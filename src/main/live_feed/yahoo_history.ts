import { type BrowserWindow } from 'electron';
import { logger } from '../logger';
import { SEED_SYMBOLS } from '../symbol_registry';
import { type YahooFinanceClient } from './yahoo_instance';

const LOOKBACK_DAYS = 7;
const INTERVAL = '1m' as const;
const FETCH_CONCURRENCY = 4;

export interface BackfillPoint {
  t: number;
  price: number;
}

export interface BackfillBatch {
  symbol_id: number;
  points: BackfillPoint[];
}

interface YahooChartQuote {
  date: Date;
  close: number | null;
}

// Cache of fetched backfill batches, so a renderer that mounts after the
// initial `webContents.send` push can pull the data via the query handler.
const cache = new Map<number, BackfillBatch>();

function quotes_to_points(quotes: YahooChartQuote[]): BackfillPoint[] {
  const out: BackfillPoint[] = [];
  for (const q of quotes) {
    if (q.close == null) continue;
    out.push({ t: q.date.getTime(), price: q.close });
  }
  return out;
}

async function fetch_one(
  client: YahooFinanceClient,
  symbol_id: number,
  ticker: string,
  period1: Date,
): Promise<BackfillBatch | null> {
  try {
    const result = await client.chart(ticker, { period1, interval: INTERVAL });
    const points = quotes_to_points(result.quotes);
    return { symbol_id, points };
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    logger.warn({ ticker, err: msg }, 'history: chart fetch failed');
    return null;
  }
}

export async function backfill_history(
  client: YahooFinanceClient,
  window: BrowserWindow,
): Promise<void> {
  const period1 = new Date(Date.now() - LOOKBACK_DAYS * 24 * 3600 * 1000);
  logger.info({ symbol_count: SEED_SYMBOLS.length, lookback_days: LOOKBACK_DAYS }, 'history: backfill starting');

  let cursor = 0;
  let total_points = 0;
  const workers = Array.from({ length: FETCH_CONCURRENCY }, async () => {
    while (cursor < SEED_SYMBOLS.length) {
      const idx = cursor++;
      const sym = SEED_SYMBOLS[idx];
      const batch = await fetch_one(client, sym.id, sym.ticker, period1);
      if (batch && batch.points.length > 0) {
        cache.set(batch.symbol_id, batch);
        if (!window.isDestroyed()) {
          window.webContents.send('history:backfill', batch);
        }
        total_points += batch.points.length;
      }
    }
  });
  await Promise.all(workers);

  logger.info({ total_points, cached_symbols: cache.size }, 'history: backfill complete');
}

export function get_cached_backfill(): BackfillBatch[] {
  return Array.from(cache.values());
}

export function clear_backfill_cache(): void {
  cache.clear();
}
