import { contextBridge, ipcRenderer, type IpcRendererEvent } from 'electron';

export interface PreloadTick {
  symbol_id: number;
  volume: number;
  price: number;
  ts_ns: number;
  seq: number;
}

export interface SymbolEntry {
  id: number;
  ticker: string;
}

export type FeedState = 'idle' | 'polling' | 'degraded' | 'error';

export interface FeedStatus {
  state: FeedState;
  source: string;
  last_poll_at: number | null;
  last_error: string | null;
  consecutive_failures: number;
}

export interface BackfillPoint {
  t: number;
  price: number;
}

export interface BackfillBatch {
  symbol_id: number;
  points: BackfillPoint[];
}

export interface SymbolSummary {
  symbol_id: number;
  month_ago_price: number;
  month_ago_t: number;
}

export interface TradingApi {
  on_ticks(handler: (batch: PreloadTick[]) => void): () => void;
  list_symbols(): Promise<SymbolEntry[]>;
  subscribe(symbol_ids: number[]): Promise<{ ok: boolean }>;
  unsubscribe(symbol_ids: number[]): Promise<{ ok: boolean }>;
  on_feed_status(handler: (status: FeedStatus) => void): () => void;
  query_feed_status(): Promise<FeedStatus>;
  on_history_backfill(handler: (batch: BackfillBatch) => void): () => void;
  query_history_backfill(): Promise<BackfillBatch[]>;
  on_summary_update(handler: (summary: SymbolSummary) => void): () => void;
  query_summaries(): Promise<SymbolSummary[]>;
}

const api: TradingApi = {
  on_ticks(handler) {
    const listener = (_event: IpcRendererEvent, batch: PreloadTick[]) => handler(batch);
    ipcRenderer.on('ticks', listener);
    return () => {
      ipcRenderer.off('ticks', listener);
    };
  },
  list_symbols() {
    return ipcRenderer.invoke('symbols:list');
  },
  subscribe(symbol_ids) {
    return ipcRenderer.invoke('subscribe', { symbol_ids });
  },
  unsubscribe(symbol_ids) {
    return ipcRenderer.invoke('unsubscribe', { symbol_ids });
  },
  on_feed_status(handler) {
    const listener = (_event: IpcRendererEvent, status: FeedStatus) => handler(status);
    ipcRenderer.on('feed:status', listener);
    return () => {
      ipcRenderer.off('feed:status', listener);
    };
  },
  query_feed_status() {
    return ipcRenderer.invoke('feed:status:get');
  },
  on_history_backfill(handler) {
    const listener = (_event: IpcRendererEvent, batch: BackfillBatch) => handler(batch);
    ipcRenderer.on('history:backfill', listener);
    return () => {
      ipcRenderer.off('history:backfill', listener);
    };
  },
  query_history_backfill() {
    return ipcRenderer.invoke('history:backfill:query');
  },
  on_summary_update(handler) {
    const listener = (_event: IpcRendererEvent, summary: SymbolSummary) => handler(summary);
    ipcRenderer.on('summary:update', listener);
    return () => {
      ipcRenderer.off('summary:update', listener);
    };
  },
  query_summaries() {
    return ipcRenderer.invoke('summary:query');
  },
};

contextBridge.exposeInMainWorld('trading', api);
