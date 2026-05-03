import { ipcMain } from 'electron';
import { event_bridge } from './event_bridge';
import { logger } from './logger';
import { SEED_SYMBOLS } from './symbol_registry';

/**
 * Wires IPC handlers shared between renderers. The subscribe/unsubscribe
 * round-trips currently no-op — every seeded symbol is polled unconditionally
 * by the daemon today. Kept as a stable surface for future per-symbol
 * subscription control.
 *
 * History backfill and monthly-summary queries are answered from the
 * EventBridge cache, which is populated by frames pushed from the C++ daemon
 * over the loopback event channel.
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

  ipcMain.handle('history:backfill:query', () => event_bridge.get_backfill_cache());

  ipcMain.handle('summary:query', () => event_bridge.get_summary_cache());
}
