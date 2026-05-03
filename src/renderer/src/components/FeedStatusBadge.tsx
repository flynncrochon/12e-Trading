import { useEffect, useState } from 'react';
import type { FeedStatus } from '../../../preload';

function format_age(now: number, last: number | null): string {
  if (last === null) return 'never';
  const seconds = Math.max(0, Math.round((now - last) / 1000));
  if (seconds < 60) return `${seconds}s ago`;
  const minutes = Math.floor(seconds / 60);
  return `${minutes}m ago`;
}

function label_for(status: FeedStatus, now: number): string {
  const age = format_age(now, status.last_poll_at);
  switch (status.state) {
    case 'idle':
      return 'starting…';
    case 'polling':
      return `live · ${age}`;
    case 'degraded':
      return `retrying · last ok ${age}`;
    case 'error':
      return `feed error: ${status.last_error ?? 'unknown'}`;
  }
}

export function FeedStatusBadge() {
  const [status, set_status] = useState<FeedStatus | null>(null);
  const [now, set_now] = useState(Date.now());

  useEffect(() => {
    let cancelled = false;
    window.trading.query_feed_status().then((s) => {
      if (!cancelled) set_status(s);
    });
    const unsubscribe = window.trading.on_feed_status((s) => set_status(s));
    return () => {
      cancelled = true;
      unsubscribe();
    };
  }, []);

  useEffect(() => {
    const id = setInterval(() => set_now(Date.now()), 500);
    return () => clearInterval(id);
  }, []);

  const state = status?.state ?? 'idle';
  const text = status ? label_for(status, now) : 'starting…';
  const source = status?.source ?? 'yahoo';

  return (
    <div className={`feed-badge feed-badge-${state}`} title={`source: ${source}`}>
      <span className={`feed-dot feed-dot-${state}`} />
      <span className="feed-badge-text">{text}</span>
    </div>
  );
}
