export interface SymbolRecord {
  id: number;
  ticker: string;
  exchange: string;
}

export const SEED_SYMBOLS: ReadonlyArray<SymbolRecord> = [
  { id: 0,  ticker: 'AAPL',   exchange: 'NASDAQ' },
  { id: 1,  ticker: 'MSFT',   exchange: 'NASDAQ' },
  { id: 2,  ticker: 'NVDA',   exchange: 'NASDAQ' },
  { id: 3,  ticker: 'TSLA',   exchange: 'NASDAQ' },
  { id: 4,  ticker: 'GOOG',   exchange: 'NASDAQ' },
  { id: 5,  ticker: 'META',   exchange: 'NASDAQ' },
  { id: 6,  ticker: 'AMZN',   exchange: 'NASDAQ' },
  { id: 7,  ticker: 'SPY',    exchange: 'NYSE'   },
  { id: 8,  ticker: 'BHP.AX', exchange: 'ASX'    },
  { id: 9,  ticker: 'CBA.AX', exchange: 'ASX'    },
  { id: 10, ticker: 'CSL.AX', exchange: 'ASX'    },
  { id: 11, ticker: 'NAB.AX', exchange: 'ASX'    },
  { id: 12, ticker: 'WBC.AX', exchange: 'ASX'    },
  { id: 13, ticker: 'ANZ.AX', exchange: 'ASX'    },
  { id: 14, ticker: 'FMG.AX', exchange: 'ASX'    },
  { id: 15, ticker: 'RIO.AX', exchange: 'ASX'    },
];

const ticker_index: ReadonlyMap<string, number> = new Map(
  SEED_SYMBOLS.map((s) => [s.ticker, s.id]),
);

const id_index: ReadonlyMap<number, string> = new Map(
  SEED_SYMBOLS.map((s) => [s.id, s.ticker]),
);

export function ticker_to_id(ticker: string): number | undefined {
  return ticker_index.get(ticker);
}

export function id_to_ticker(id: number): string | undefined {
  return id_index.get(id);
}

export function all_tickers(): string[] {
  return SEED_SYMBOLS.map((s) => s.ticker);
}
