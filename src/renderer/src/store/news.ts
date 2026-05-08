/* eslint-disable react-hooks/rules-of-hooks --
   Repo convention: hooks are snake_case (use_news_items). The react-hooks
   plugin only matches the camelCase `use[A-Z]` form and treats ours as plain
   functions — disable here, hook call-site rules still apply manually. */
import { useSyncExternalStore } from 'react';

export interface SentimentScore {
  label: 'positive' | 'negative' | 'neutral';
  score: number;
}

export interface NewsItem {
  ticker: string;
  short_name: string;
  news_id: string;
  title: string;
  publisher: string;
  link: string;
  published_t: number;
  stock_pct: number;
  benchmark: string;
  benchmark_pct: number;
  adjusted_pct: number;
  daily_change_pct: number;
  sentiment: SentimentScore | null;
}

type Listener = () => void;

const items = new Map<string, NewsItem>();
const listeners = new Set<Listener>();

let snapshot: NewsItem[] = [];

function rebuild_snapshot(): void {
  snapshot = Array.from(items.values());
}

function notify(): void {
  for (const l of listeners) l();
}

export function ingest_news(item: NewsItem): void {
  items.set(item.news_id, item);
  rebuild_snapshot();
  notify();
}

export function ingest_news_batch(batch: NewsItem[]): void {
  if (batch.length === 0) return;
  for (const item of batch) {
    items.set(item.news_id, item);
  }
  rebuild_snapshot();
  notify();
}

function subscribe(listener: Listener): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/**
 * Returns all known news items as a stable-reference array. The reference
 * only changes when the underlying map mutates, which keeps
 * useSyncExternalStore's Object.is check from firing on every render.
 */
export function use_news_items(): NewsItem[] {
  return useSyncExternalStore(subscribe, () => snapshot);
}

export function reset_for_tests(): void {
  items.clear();
  listeners.clear();
  snapshot = [];
}
