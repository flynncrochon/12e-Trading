import { type BrowserWindow } from 'electron';
import { logger } from '../logger';
import { all_tickers, ticker_to_id } from '../symbol_registry';
import { type IpcTick } from '../tick_bridge';
import { backfill_history, clear_backfill_cache } from './yahoo_history';
import { get_yahoo, reset_yahoo } from './yahoo_instance';
import { clear_summary_cache, fetch_summaries } from './yahoo_summary';

const POLL_INTERVAL_MS = 10 * 60 * 1000;
const BACKOFF_STEPS_MS = [5_000, 15_000, 60_000, 5 * 60_000];

interface YahooQuoteShape {
  symbol: string;
  regularMarketPrice?: number | null;
  regularMarketVolume?: number | null;
}

let poll_timer: NodeJS.Timeout | null = null;
let in_flight = false;
let stopping = false;
let consecutive_failures = 0;
let seq_counter = 0;
let backfill_started = false;
const last_volume = new Map<number, number>();

function next_delay_ms(): number {
  if (consecutive_failures === 0) return POLL_INTERVAL_MS;
  const idx = Math.min(consecutive_failures - 1, BACKOFF_STEPS_MS.length - 1);
  return BACKOFF_STEPS_MS[idx];
}

function quotes_to_ticks(quotes: YahooQuoteShape[]): IpcTick[] {
  const ts_ns = Date.now() * 1_000_000;
  const out: IpcTick[] = [];
  for (const q of quotes) {
    const symbol_id = ticker_to_id(q.symbol);
    if (symbol_id === undefined) continue;
    if (q.regularMarketPrice == null) continue;

    const current_volume = q.regularMarketVolume ?? 0;
    const prior = last_volume.get(symbol_id);
    const volume_delta = prior === undefined ? 0 : Math.max(0, current_volume - prior);
    last_volume.set(symbol_id, current_volume);

    seq_counter = (seq_counter + 1) >>> 0;

    out.push({
      symbol_id,
      volume: volume_delta,
      price: q.regularMarketPrice,
      ts_ns,
      seq: seq_counter,
    });
  }
  return out;
}

async function poll_once(window: BrowserWindow): Promise<void> {
  if (in_flight || stopping) return;
  in_flight = true;
  const tickers = all_tickers();
  try {
    const yahoo = await get_yahoo();
    const result = await yahoo.quote(tickers, {}, { validateResult: false });
    const list: YahooQuoteShape[] = Array.isArray(result)
      ? (result as YahooQuoteShape[])
      : [result as YahooQuoteShape];
    const ticks = quotes_to_ticks(list);

    if (ticks.length > 0 && !window.isDestroyed()) {
      window.webContents.send('ticks', ticks);
    }

    consecutive_failures = 0;
    logger.debug({ count: ticks.length }, 'yahoo: poll ok');

    if (!backfill_started) {
      backfill_started = true;
      // Fire-and-forget: backfill + summary run alongside the polling loop.
      // Errors are logged inside each function and don't disturb polling.
      backfill_history(yahoo, window).catch((err) => {
        logger.error({ err: String(err) }, 'history: backfill driver crashed');
      });
      fetch_summaries(yahoo, window).catch((err) => {
        logger.error({ err: String(err) }, 'summary: fetch driver crashed');
      });
    }
  } catch (err) {
    consecutive_failures += 1;
    const msg = err instanceof Error ? err.message : String(err);
    logger.warn({ err: msg, consecutive_failures }, 'yahoo: poll failed');
  } finally {
    in_flight = false;
    schedule_next(window);
  }
}

function schedule_next(window: BrowserWindow): void {
  if (stopping) return;
  if (poll_timer) clearTimeout(poll_timer);
  poll_timer = setTimeout(() => poll_once(window), next_delay_ms());
}

export function start_yahoo_feed(window: BrowserWindow): void {
  if (poll_timer) return;
  stopping = false;
  consecutive_failures = 0;
  seq_counter = 0;
  backfill_started = false;
  last_volume.clear();
  clear_backfill_cache();
  clear_summary_cache();

  logger.info({ symbol_count: all_tickers().length }, 'yahoo: feed starting');
  poll_once(window);
}

export function stop_yahoo_feed(): void {
  stopping = true;
  if (poll_timer) {
    clearTimeout(poll_timer);
    poll_timer = null;
  }
  reset_yahoo();
  logger.info('yahoo: feed stopped');
}
