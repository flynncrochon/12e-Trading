interface YahooQuote {
  symbol: string;
  regularMarketPrice?: number | null;
  regularMarketVolume?: number | null;
}

interface YahooChartQuote {
  date: Date;
  close: number | null;
}

export type ChartInterval = '1m' | '1d';

export interface YahooFinanceClient {
  quote(
    symbols: string[],
    queryOpts?: object,
    moduleOpts?: { validateResult?: boolean },
  ): Promise<YahooQuote | YahooQuote[]>;
  chart(
    symbol: string,
    opts: { period1: Date; interval: ChartInterval },
  ): Promise<{ quotes: YahooChartQuote[] }>;
}

interface YahooFinanceCtor {
  new (opts?: { suppressNotices?: string[] }): YahooFinanceClient;
}

let instance: YahooFinanceClient | null = null;

export async function get_yahoo(): Promise<YahooFinanceClient> {
  if (instance) return instance;
  // Dynamic import: yahoo-finance2 v3 is ESM-only and the main bundle is CJS.
  // The bare specifier is wrapped to keep TS from rewriting `import()` to `require()`.
  const specifier = 'yahoo-finance2';
  const mod = (await import(specifier)) as { default: YahooFinanceCtor };
  instance = new mod.default({
    suppressNotices: ['yahooSurvey', 'ripHistorical'],
  });
  return instance;
}

export function reset_yahoo(): void {
  instance = null;
}
