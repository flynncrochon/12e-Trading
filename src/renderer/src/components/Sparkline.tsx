import { useMemo } from 'react';
import { Line, LineChart, ResponsiveContainer, XAxis, YAxis } from 'recharts';
import {
  get_history_extent,
  read_history_since,
  use_history_version,
} from '../store/prices';

interface SparklineProps {
  symbol_id: number;
  /** Lookback window in ms, anchored at the latest history point. Defaults to 1h. */
  window_ms?: number;
}

const SPARK_DEFAULT_WINDOW_MS = 60 * 60_000;

export function Sparkline({ symbol_id, window_ms = SPARK_DEFAULT_WINDOW_MS }: SparklineProps) {
  const version = use_history_version(symbol_id);

  const points = useMemo(
    () => {
      const extent = get_history_extent(symbol_id);
      if (!extent) return [];
      const since = extent.latest - window_ms;
      return read_history_since(symbol_id, since);
    },
    // version intentionally drives the recompute even though it's not read directly
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [symbol_id, window_ms, version],
  );

  if (points.length < 2) {
    return <div className="sparkline sparkline-empty" />;
  }

  // Color the line by net direction over the visible window.
  const stroke =
    points[points.length - 1].price >= points[0].price
      ? 'var(--up)'
      : 'var(--down)';

  // Anchor the X domain to a fixed window-length so the time scale stays
  // constant as new live ticks arrive (otherwise the chart visually
  // "extends" because high-frequency live ticks dominate the right edge).
  const latest_t = points[points.length - 1].t;
  const x_min = latest_t - window_ms;

  return (
    <div className="sparkline">
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={points} margin={{ top: 2, right: 2, bottom: 2, left: 2 }}>
          <XAxis
            dataKey="t"
            type="number"
            domain={[x_min, latest_t]}
            hide
          />
          <YAxis hide domain={['dataMin', 'dataMax']} />
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
    </div>
  );
}
