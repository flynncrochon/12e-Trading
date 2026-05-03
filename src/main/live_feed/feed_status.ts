import { type BrowserWindow } from 'electron';

export type FeedState = 'idle' | 'polling' | 'degraded' | 'error';

export interface FeedStatus {
  state: FeedState;
  source: string;
  last_poll_at: number | null;
  last_error: string | null;
  consecutive_failures: number;
}

let current: FeedStatus = {
  state: 'idle',
  source: 'yahoo',
  last_poll_at: null,
  last_error: null,
  consecutive_failures: 0,
};

export function publish_status(window: BrowserWindow | null, partial: Partial<FeedStatus>): void {
  current = { ...current, ...partial };
  if (window && !window.isDestroyed()) {
    window.webContents.send('feed:status', current);
  }
}

export function get_current_status(): FeedStatus {
  return current;
}
