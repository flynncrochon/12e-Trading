/* eslint-disable react-hooks/rules-of-hooks --
   This repo uses snake_case for hook names (e.g. `use_tick_stream`); the
   react-hooks plugin only recognises the camelCase `use[A-Z]` form. The
   *call-site* checks the rule also covers (no hooks in conditions/loops)
   are still satisfied — they just can't be enforced by the linter here. */
import { useEffect } from 'react';
import { ingest_news, ingest_news_batch } from '../store/news';
import { ingest_history, ingest_ticks, set_summary } from '../store/prices';
import type { Tick } from '../types/tick';

/**
 * Subscribes to tick batches, historical backfill, and per-symbol summaries
 * from the main process, feeding all three into the in-memory price store.
 * Mount once at the app root.
 */
export function use_tick_stream(): void {
  useEffect(() => {
    const unsub_ticks = window.trading.on_ticks((batch: Tick[]) => {
      ingest_ticks(batch);
    });
    const unsub_history = window.trading.on_history_backfill((batch) => {
      ingest_history(batch.symbol_id, batch.points);
    });
    const unsub_summary = window.trading.on_summary_update((s) => {
      set_summary(s.symbol_id, {
        month_ago_price: s.month_ago_price,
        month_ago_t: s.month_ago_t,
      });
    });
    const unsub_news = window.trading.on_news_item((item) => {
      ingest_news(item);
    });
    // Pull anything cached before this hook subscribed — backfill, summary,
    // and news messages can be sent before the React effect runs, so the
    // initial round can otherwise be missed entirely.
    let cancelled = false;
    window.trading.query_history_backfill().then((batches) => {
      if (cancelled) return;
      for (const b of batches) ingest_history(b.symbol_id, b.points);
    });
    window.trading.query_summaries().then((list) => {
      if (cancelled) return;
      for (const s of list) {
        set_summary(s.symbol_id, {
          month_ago_price: s.month_ago_price,
          month_ago_t: s.month_ago_t,
        });
      }
    });
    window.trading.query_news().then((list) => {
      if (cancelled) return;
      ingest_news_batch(list);
    });
    return () => {
      cancelled = true;
      unsub_ticks();
      unsub_history();
      unsub_summary();
      unsub_news();
    };
  }, []);
}
