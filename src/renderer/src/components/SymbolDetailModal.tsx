import { useEffect, useMemo, useRef, type MouseEvent } from 'react';
import {
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import {
  read_history_since,
  use_history_version,
  use_price,
  type HistoryPoint,
} from '../store/prices';
import type { SymbolEntry } from '../types/tick';

interface SymbolDetailModalProps {
  symbol: SymbolEntry;
  on_close: () => void;
}

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

function pad2(n: number): string {
  return n.toString().padStart(2, '0');
}

function format_clock(t: number): string {
  const d = new Date(t);
  return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
}

/**
 * Picks a tick format based on the visible time span. Charts spanning days
 * need dates; intra-day charts only need clock time.
 */
function format_axis_tick(t: number, span_ms: number): string {
  const d = new Date(t);
  const month = MONTHS[d.getMonth()];
  const day = d.getDate();
  if (span_ms >= 3 * 24 * 3600 * 1000) {
    return `${month} ${day}`;
  }
  if (span_ms >= 24 * 3600 * 1000) {
    return `${month} ${day} ${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
  }
  if (span_ms >= 60 * 60 * 1000) {
    return `${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
  }
  return format_clock(t);
}

function format_tooltip_time(t: number, span_ms: number): string {
  const d = new Date(t);
  const month = MONTHS[d.getMonth()];
  const day = d.getDate();
  const hms = format_clock(t);
  if (span_ms >= 24 * 3600 * 1000) {
    return `${month} ${day} ${hms}`;
  }
  return hms;
}

export function SymbolDetailModal({ symbol, on_close }: SymbolDetailModalProps) {
  const price = use_price(symbol.id);
  const version = use_history_version(symbol.id);
  const overlay_ref = useRef<HTMLDivElement>(null);

  const points = useMemo(
    () => read_history_since(symbol.id, 0),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [symbol.id, version],
  );

  const span_ms = points.length >= 2
    ? points[points.length - 1].t - points[0].t
    : 0;

  // Esc to close.
  useEffect(() => {
    const on_key = (e: KeyboardEvent) => {
      if (e.key === 'Escape') on_close();
    };
    window.addEventListener('keydown', on_key);
    return () => window.removeEventListener('keydown', on_key);
  }, [on_close]);

  const handle_overlay_click = (e: MouseEvent) => {
    if (e.target === overlay_ref.current) on_close();
  };

  const stroke =
    points.length >= 2 && points[points.length - 1].price >= points[0].price
      ? 'var(--up)'
      : 'var(--down)';

  return (
    <div
      ref={overlay_ref}
      className="modal-overlay"
      onClick={handle_overlay_click}
      role="dialog"
      aria-modal="true"
    >
      <div className="modal-card">
        <header className="modal-header">
          <div>
            <div className="modal-ticker">{symbol.ticker}</div>
            {price && (
              <div className="modal-price">
                {price.price.toFixed(4)}
                <span className="modal-seq">#{price.seq}</span>
              </div>
            )}
          </div>
          <button className="modal-close" onClick={on_close} aria-label="Close">
            ✕
          </button>
        </header>

        <div className="chart-container">
          {points.length < 2 ? (
            <div className="chart-empty">
              waiting for data — need at least 2 points
            </div>
          ) : (
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={points} margin={{ top: 8, right: 16, bottom: 8, left: 56 }}>
                <CartesianGrid stroke="var(--border)" strokeDasharray="3 3" />
                <XAxis
                  dataKey="t"
                  type="number"
                  domain={['dataMin', 'dataMax']}
                  scale="time"
                  tickFormatter={(v: number) => format_axis_tick(v, span_ms)}
                  stroke="var(--text-dim)"
                  tick={{ fontSize: 11 }}
                  minTickGap={50}
                />
                <YAxis
                  domain={['auto', 'auto']}
                  stroke="var(--text-dim)"
                  tick={{ fontSize: 11 }}
                  tickFormatter={(v: number) => v.toFixed(2)}
                  width={56}
                />
                <Tooltip
                  cursor={{ stroke: 'var(--text-dim)' }}
                  content={(props) => {
                    if (!props.active || !props.payload || props.payload.length === 0) {
                      return null;
                    }
                    const p = props.payload[0].payload as HistoryPoint;
                    return (
                      <div className="chart-tooltip">
                        <div className="chart-tooltip-time">{format_tooltip_time(p.t, span_ms)}</div>
                        <div className="chart-tooltip-price">{p.price.toFixed(4)}</div>
                      </div>
                    );
                  }}
                />
                <Line
                  type="monotone"
                  dataKey="price"
                  stroke={stroke}
                  strokeWidth={1.5}
                  dot={false}
                  isAnimationActive={false}
                />
              </LineChart>
            </ResponsiveContainer>
          )}
        </div>
      </div>
    </div>
  );
}
