import { useEffect, useState } from 'react';
import { use_feed_stats } from '../store/prices';

export function StatusBar() {
  const stats = use_feed_stats();
  const [now, set_now] = useState(Date.now());

  useEffect(() => {
    const id = setInterval(() => set_now(Date.now()), 500);
    return () => clearInterval(id);
  }, []);

  const stale = stats.last_update_at > 0 && now - stats.last_update_at > 1500;
  const indicator = stats.last_update_at === 0 ? 'waiting' : stale ? 'stale' : 'live';

  return (
    <div className="status-bar">
      <span className={`dot dot-${indicator}`} />
      <span>{indicator}</span>
      <span className="spacer" />
      <span>{stats.total_ticks.toLocaleString()} ticks</span>
    </div>
  );
}
