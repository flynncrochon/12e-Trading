import { PriceRow } from './PriceRow';
import type { SymbolEntry } from '../types/tick';

interface SymbolListProps {
  symbols: SymbolEntry[];
  on_select?: (symbol: SymbolEntry) => void;
}

export function SymbolList({ symbols, on_select }: SymbolListProps) {
  return (
    <div className="symbol-list">
      <div className="price-row header">
        <span className="ticker">Symbol</span>
        <span className="price">Price</span>
        <span className="volume">Volume</span>
        <span className="spark">1h</span>
        <span className="month-pct">1mo</span>
        <span className="seq">Seq</span>
      </div>
      {symbols.map((s) => (
        <PriceRow key={s.id} symbol={s} on_click={on_select} />
      ))}
    </div>
  );
}
