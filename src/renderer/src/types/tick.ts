export interface Tick {
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

export interface PriceState {
  price: number;
  last_price: number;
  volume: number;
  ts_ns: number;
  seq: number;
  /** Wall-clock timestamp of the last price update (ms since epoch). */
  updated_at: number;
}
