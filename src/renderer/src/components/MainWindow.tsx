import { useEffect, useState } from 'react';
import { use_tick_stream } from '../hooks/use_tick_stream';
import { StatusBar } from './StatusBar';
import { SymbolList } from './SymbolList';
import { SymbolDetailModal } from './SymbolDetailModal';
import type { SymbolEntry } from '../types/tick';

export function MainWindow() {
  use_tick_stream();
  const [symbols, set_symbols] = useState<SymbolEntry[]>([]);
  const [selected, set_selected] = useState<SymbolEntry | null>(null);

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
      <main className="content">
        <SymbolList symbols={symbols} on_select={set_selected} />
      </main>
      <StatusBar />
      {selected && (
        <SymbolDetailModal symbol={selected} on_close={() => set_selected(null)} />
      )}
    </div>
  );
}
