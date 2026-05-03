import { ipcMain } from 'electron';
import { get_cached_backfill } from './live_feed/yahoo_history';
import { get_cached_summaries } from './live_feed/yahoo_summary';
import { logger } from './logger';
import { SEED_SYMBOLS } from './symbol_registry';

/**
 * Wires IPC handlers shared between renderers. The subscribe/unsubscribe
 * round-trips currently no-op — every seeded symbol is polled unconditionally
 * by the live feed today. Kept as a stable surface for future per-symbol
 * subscription control.
 */
export function register_ipc_handlers(): void {
  ipcMain.handle('symbols:list', () => {
    return SEED_SYMBOLS.map(({ id, ticker }) => ({ id, ticker }));
  });

  ipcMain.handle('subscribe', (_event, payload: { symbol_ids: number[] }) => {
    logger.debug({ payload }, 'subscribe (placeholder — feed polls all seeded symbols)');
    return { ok: true };
  });

  ipcMain.handle('unsubscribe', (_event, payload: { symbol_ids: number[] }) => {
    logger.debug({ payload }, 'unsubscribe (placeholder — feed polls all seeded symbols)');
    return { ok: true };
  });

  ipcMain.handle('history:backfill:query', () => get_cached_backfill());

  ipcMain.handle('summary:query', () => get_cached_summaries());
}
