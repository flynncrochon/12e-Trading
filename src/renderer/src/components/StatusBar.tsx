import { use_feed_stats } from '../store/prices';

export function StatusBar() {
  const stats = use_feed_stats();

  return (
    <div className="status-bar">
      <span>{stats.total_ticks.toLocaleString()} ticks</span>
    </div>
  );
}
