import { memo, useEffect, useRef, useState, type KeyboardEvent } from 'react';
import { use_price, use_summary } from '../store/prices';
import type { SymbolEntry } from '../types/tick';
import { Sparkline } from './Sparkline';

interface PriceRowProps {
  symbol: SymbolEntry;
  on_click?: (symbol: SymbolEntry) => void;
}

const FLASH_DURATION_MS = 250;

function format_price(p: number): string {
  return p.toFixed(2);
}

function format_volume(v: number): string {
  return v.toLocaleString();
}

function format_pct(p: number): string {
  const sign = p >= 0 ? '+' : '';
  return `${sign}${p.toFixed(2)}%`;
}

function PriceRowImpl({ symbol, on_click }: PriceRowProps) {
  const price = use_price(symbol.id);
  const summary = use_summary(symbol.id);
  const [flash, set_flash] = useState<'up' | 'down' | null>(null);
  const flash_timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (!price) return;
    if (price.last_price === price.price) return;
    const dir = price.price > price.last_price ? 'up' : 'down';
    set_flash(dir);
    if (flash_timer.current) clearTimeout(flash_timer.current);
    flash_timer.current = setTimeout(() => set_flash(null), FLASH_DURATION_MS);
  }, [price]);

  useEffect(() => {
    return () => {
      if (flash_timer.current) clearTimeout(flash_timer.current);
    };
  }, []);

  const flash_class = flash === 'up' ? 'flash-up' : flash === 'down' ? 'flash-down' : '';
  const interactive = on_click ? 'price-row-interactive' : '';

  const month_pct = price && summary
    ? ((price.price - summary.month_ago_price) / summary.month_ago_price) * 100
    : null;
  const month_class = month_pct == null ? '' : month_pct >= 0 ? 'pct-up' : 'pct-down';

  const handle_click = () => on_click?.(symbol);
  const handle_key = (e: KeyboardEvent) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      on_click?.(symbol);
    }
  };

  return (
    <div
      className={`price-row ${flash_class} ${interactive}`}
      onClick={on_click ? handle_click : undefined}
      onKeyDown={on_click ? handle_key : undefined}
      role={on_click ? 'button' : undefined}
      tabIndex={on_click ? 0 : undefined}
    >
      <span className="ticker">{symbol.ticker}</span>
      <span className="price">{price ? format_price(price.price) : '—'}</span>
      <span className="volume">{price ? format_volume(price.volume) : '—'}</span>
      <span className="spark">
        <Sparkline symbol_id={symbol.id} />
      </span>
      <span className={`month-pct ${month_class}`}>
        {month_pct == null ? '—' : format_pct(month_pct)}
      </span>
      <span className="seq">{price ? `#${price.seq}` : ''}</span>
    </div>
  );
}

export const PriceRow = memo(PriceRowImpl);
