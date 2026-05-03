import { type BrowserWindow } from 'electron';
import { existsSync } from 'node:fs';
import { logger } from './logger';
import { resolve_reader_addon_path } from './paths';

const POLL_INTERVAL_MS = 16;
const MAX_TICKS_PER_POLL = 1024;
const ATTACH_RETRY_INTERVAL_MS = 200;
const ATTACH_TIMEOUT_MS = 10_000;

export interface IpcTick {
  symbol_id: number;
  volume: number;
  price: number;
  ts_ns: number;
  seq: number;
}

interface ShmReaderAddon {
  open: () => boolean;
  close: () => void;
  is_open: () => boolean;
  poll_ticks: (max_n: number) => IpcTick[];
}

let addon: ShmReaderAddon | null = null;
let poll_timer: NodeJS.Timeout | null = null;
let attach_timer: NodeJS.Timeout | null = null;

function load_addon(): ShmReaderAddon | null {
  const path = resolve_reader_addon_path();
  if (!existsSync(path)) {
    logger.error({ path }, 'tick-bridge: shm_reader.node not found — did you run pnpm build:native?');
    return null;
  }
  try {
    return require(path) as ShmReaderAddon;
  } catch (err) {
    logger.error({ err: String(err), path }, 'tick-bridge: failed to load native addon');
    return null;
  }
}

/**
 * Attempts to attach to the shared-memory region produced by the daemon,
 * retrying until success or a timeout. Resolves with `true` on success.
 */
export function attach_to_daemon(): Promise<boolean> {
  if (!addon) addon = load_addon();
  if (!addon) return Promise.resolve(false);

  return new Promise<boolean>((resolve) => {
    const started_at = Date.now();
    const try_once = () => {
      if (!addon) {
        resolve(false);
        return;
      }
      if (addon.open()) {
        logger.info('tick-bridge: attached to shared memory');
        resolve(true);
        return;
      }
      if (Date.now() - started_at > ATTACH_TIMEOUT_MS) {
        logger.error('tick-bridge: timed out waiting for daemon shared memory');
        resolve(false);
        return;
      }
      attach_timer = setTimeout(try_once, ATTACH_RETRY_INTERVAL_MS);
    };
    try_once();
  });
}

export function start_poll_loop(window: BrowserWindow): void {
  if (!addon) {
    logger.error('tick-bridge: cannot start poll loop, addon not loaded');
    return;
  }
  if (poll_timer) return;

  poll_timer = setInterval(() => {
    try {
      const batch = addon!.poll_ticks(MAX_TICKS_PER_POLL);
      if (batch.length > 0 && !window.isDestroyed()) {
        window.webContents.send('ticks', batch);
      }
    } catch (err) {
      logger.error({ err: String(err) }, 'tick-bridge: poll error');
    }
  }, POLL_INTERVAL_MS);
}

export function stop_poll_loop(): void {
  if (attach_timer) {
    clearTimeout(attach_timer);
    attach_timer = null;
  }
  if (poll_timer) {
    clearInterval(poll_timer);
    poll_timer = null;
  }
  if (addon?.is_open()) {
    addon.close();
  }
}
