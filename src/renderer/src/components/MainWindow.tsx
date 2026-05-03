import { useEffect, useState } from 'react';
import { use_tick_stream } from '../hooks/use_tick_stream';
import { NewsTab } from './NewsTab';
import { StatusBar } from './StatusBar';
import { SymbolList } from './SymbolList';
import { SymbolDetailModal } from './SymbolDetailModal';
import type { SymbolEntry } from '../types/tick';

type Tab = 'stocks' | 'news';

export function MainWindow() {
  use_tick_stream();
  const [symbols, set_symbols] = useState<SymbolEntry[]>([]);
  const [selected, set_selected] = useState<SymbolEntry | null>(null);
  const [active_tab, set_active_tab] = useState<Tab>('stocks');

  useEffect(() => {
    let cancelled = false;
    window.trading.list_symbols().then((list) => {
      if (!cancelled) set_symbols(list);
    });
    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <div className="main-window">
      <header className="app-header">
        <h1>12e Trading</h1>
        <span className="subtitle">Yahoo Finance · US + ASX · 10min updates</span>
      </header>
      <nav className="tab-bar" role="tablist">
        <button
          role="tab"
          aria-selected={active_tab === 'stocks'}
          className={`tab ${active_tab === 'stocks' ? 'active' : ''}`}
          onClick={() => set_active_tab('stocks')}
        >
          Stocks
        </button>
        <button
          role="tab"
          aria-selected={active_tab === 'news'}
          className={`tab ${active_tab === 'news' ? 'active' : ''}`}
          onClick={() => set_active_tab('news')}
        >
          Negative News
        </button>
      </nav>
      <main className="content">
        {active_tab === 'stocks' ? (
          <SymbolList symbols={symbols} on_select={set_selected} />
        ) : (
          <NewsTab />
        )}
      </main>
      <StatusBar />
      {selected && (
        <SymbolDetailModal symbol={selected} on_close={() => set_selected(null)} />
      )}
    </div>
  );
}
