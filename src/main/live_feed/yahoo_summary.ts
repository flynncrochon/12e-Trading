import { type BrowserWindow } from 'electron';
import { logger } from '../logger';
import { SEED_SYMBOLS } from '../symbol_registry';
import { type YahooFinanceClient } from './yahoo_instance';

const LOOKBACK_DAYS = 35;
const MONTH_AGO_DAYS = 30;
const FETCH_CONCURRENCY = 4;

export interface SymbolSummary {
  symbol_id: number;
  month_ago_price: number;
  month_ago_t: number;
}

interface DailyQuote {
  date: Date;
  close: number | null;
}

const cache = new Map<number, SymbolSummary>();

function pick_month_ago(quotes: DailyQuote[]): DailyQuote | null {
  const target = Date.now() - MONTH_AGO_DAYS * 24 * 3600 * 1000;
  let best: DailyQuote | null = null;
  let best_dist = Number.POSITIVE_INFINITY;
  for (const q of quotes) {
    if (q.close == null) continue;
    const dist = Math.abs(q.date.getTime() - target);
    if (dist < best_dist) {
      best = q;
      best_dist = dist;
    }
  }
  return best;
}

async function fetch_one(
  client: YahooFinanceClient,
  symbol_id: number,
  ticker: string,
  period1: Date,
): Promise<SymbolSummary | null> {
  try {
    const result = await client.chart(ticker, { period1, interval: '1d' });
    const month_ago = pick_month_ago(result.quotes);
    if (!month_ago || month_ago.close == null) return null;
    return {
      symbol_id,
      month_ago_price: month_ago.close,
      month_ago_t: month_ago.date.getTime(),
    };
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    logger.warn({ ticker, err: msg }, 'summary: chart fetch failed');
    return null;
  }
}

export async function fetch_summaries(
  client: YahooFinanceClient,
  window: BrowserWindow,
): Promise<void> {
  const period1 = new Date(Date.now() - LOOKBACK_DAYS * 24 * 3600 * 1000);
  logger.info({ symbol_count: SEED_SYMBOLS.length }, 'summary: fetch starting');

  let cursor = 0;
  const workers = Array.from({ length: FETCH_CONCURRENCY }, async () => {
    while (cursor < SEED_SYMBOLS.length) {
      const idx = cursor++;
      const sym = SEED_SYMBOLS[idx];
      const summary = await fetch_one(client, sym.id, sym.ticker, period1);
      if (summary) {
        cache.set(summary.symbol_id, summary);
        if (!window.isDestroyed()) {
          window.webContents.send('summary:update', summary);
        }
      }
    }
  });
  await Promise.all(workers);

  logger.info({ cached_symbols: cache.size }, 'summary: fetch complete');
}

export function get_cached_summaries(): SymbolSummary[] {
  return Array.from(cache.values());
}

export function clear_summary_cache(): void {
  cache.clear();
}
