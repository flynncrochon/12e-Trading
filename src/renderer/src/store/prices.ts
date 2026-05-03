/* eslint-disable react-hooks/rules-of-hooks --
   Repo convention: hooks are snake_case (use_price, use_feed_stats). The
   react-hooks plugin only matches the camelCase `use[A-Z]` form and treats
   ours as plain functions — disable here, hook call-site rules still apply
   manually. */
import { useSyncExternalStore } from 'react';
import type { PriceState, Tick } from '../types/tick';

type Listener = () => void;

const state = new Map<number, PriceState>();
const symbol_listeners = new Map<number, Set<Listener>>();
const global_listeners = new Set<Listener>();

let last_update_at = 0;
let total_ticks = 0;

// Per-symbol price history (ring buffer). Capped at HISTORY_CAPACITY samples
// per symbol — at 10 Hz that's 30 minutes of headroom per symbol.
export interface HistoryPoint {
  /** Wall-clock timestamp (ms since epoch). */
  t: number;
  price: number;
}

const HISTORY_CAPACITY = 18_000;
const history = new Map<number, HistoryPoint[]>();
const history_version = new Map<number, number>();

export interface SummaryEntry {
  month_ago_price: number;
  month_ago_t: number;
}

const summaries = new Map<number, SummaryEntry>();

function notify(symbol_id: number): void {
  const subs = symbol_listeners.get(symbol_id);
  if (subs) {
    for (const l of subs) l();
  }
  for (const l of global_listeners) l();
}

function append_history(symbol_id: number, t: number, price: number): void {
  let h = history.get(symbol_id);
  if (!h) {
    h = [];
    history.set(symbol_id, h);
  }
  h.push({ t, price });
  // Drop the oldest entry once over capacity. Array.shift is O(n) but at
  // 10 Hz on a 18k-element array it's negligible (V8 uses a fast path).
  if (h.length > HISTORY_CAPACITY) {
    h.shift();
  }
  history_version.set(symbol_id, (history_version.get(symbol_id) ?? 0) + 1);
}

export function ingest_ticks(batch: Tick[]): void {
  if (batch.length === 0) return;
  const now = Date.now();
  const touched = new Set<number>();

  for (const t of batch) {
    const prev = state.get(t.symbol_id);
    state.set(t.symbol_id, {
      price: t.price,
      last_price: prev?.price ?? t.price,
      volume: t.volume,
      ts_ns: t.ts_ns,
      seq: t.seq,
      updated_at: now,
    });
    // Only extend the history when the price actually moves. Polling keeps
    // streaming ticks during market-closed hours with the unchanged last
    // close, which would otherwise pad the chart with thousands of duplicate
    // points and slide the time window away from real trading data. We also
    // skip the very first tick (prev is undefined) so the chart anchor stays
    // on backfilled candles until the price genuinely moves — otherwise a
    // single "now" point would push the 1h window off the historical data.
    if (prev !== undefined && prev.price !== t.price) {
      append_history(t.symbol_id, now, t.price);
    }
    touched.add(t.symbol_id);
  }

  total_ticks += batch.length;
  last_update_at = now;
  for (const id of touched) notify(id);
}

/**
 * Merge backfilled historical points (e.g. Yahoo 1-minute candles fetched on
 * startup) into the per-symbol history buffer. Points are merged with anything
 * the live feed has already deposited, deduplicated by timestamp, kept sorted,
 * and capped at HISTORY_CAPACITY.
 *
 * Does not touch current price state or feed counters — backfill only fills
 * the chart, it isn't a "tick".
 */
export function ingest_history(symbol_id: number, points: HistoryPoint[]): void {
  if (points.length === 0) return;
  const existing = history.get(symbol_id) ?? [];
  const merged = existing.length === 0 ? points.slice() : [...points, ...existing];
  merged.sort((a, b) => a.t - b.t);

  const deduped: HistoryPoint[] = [];
  let last_t = Number.NEGATIVE_INFINITY;
  for (const p of merged) {
    if (p.t !== last_t) {
      deduped.push(p);
      last_t = p.t;
    }
  }

  const trimmed = deduped.length > HISTORY_CAPACITY
    ? deduped.slice(deduped.length - HISTORY_CAPACITY)
    : deduped;
  history.set(symbol_id, trimmed);
  history_version.set(symbol_id, (history_version.get(symbol_id) ?? 0) + 1);
  notify(symbol_id);
}

export function get_price(symbol_id: number): PriceState | undefined {
  return state.get(symbol_id);
}

export function subscribe_price(symbol_id: number, listener: Listener): () => void {
  let subs = symbol_listeners.get(symbol_id);
  if (!subs) {
    subs = new Set();
    symbol_listeners.set(symbol_id, subs);
  }
  subs.add(listener);
  return () => {
    subs!.delete(listener);
    if (subs!.size === 0) symbol_listeners.delete(symbol_id);
  };
}

export function subscribe_any(listener: Listener): () => void {
  global_listeners.add(listener);
  return () => {
    global_listeners.delete(listener);
  };
}

export function use_price(symbol_id: number): PriceState | undefined {
  return useSyncExternalStore(
    (l) => subscribe_price(symbol_id, l),
    () => state.get(symbol_id),
  );
}

/**
 * Returns a monotonically increasing version number for the given symbol's
 * history. Components subscribe via this, then `useMemo` their visible slice
 * keyed on the returned version. This avoids the
 * useSyncExternalStore-with-fresh-array infinite-loop trap.
 */
export function use_history_version(symbol_id: number): number {
  return useSyncExternalStore(
    (l) => subscribe_price(symbol_id, l),
    () => history_version.get(symbol_id) ?? 0,
  );
}

/**
 * Returns the slice of history for `symbol_id` whose timestamps are >= since_t.
 * Pass since_t = 0 (or any value before the buffer starts) to get everything.
 * Result is a fresh array — caller may freely use it as recharts data.
 */
export function read_history_since(symbol_id: number, since_t: number): HistoryPoint[] {
  const h = history.get(symbol_id);
  if (!h || h.length === 0) return [];
  if (h[0].t >= since_t) return h.slice();
  // Binary search for first index where t >= since_t.
  let lo = 0;
  let hi = h.length - 1;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (h[mid].t < since_t) lo = mid + 1;
    else hi = mid;
  }
  return h.slice(lo);
}

export function get_history_extent(symbol_id: number): { earliest: number; latest: number } | null {
  const h = history.get(symbol_id);
  if (!h || h.length === 0) return null;
  return { earliest: h[0].t, latest: h[h.length - 1].t };
}

/**
 * Returns the most recent `n` history points for `symbol_id`. Useful for
 * sparklines that should always show recent activity regardless of market
 * hours (a time-window slice is empty over the weekend).
 */
export function read_history_tail(symbol_id: number, n: number): HistoryPoint[] {
  const h = history.get(symbol_id);
  if (!h || h.length === 0) return [];
  if (h.length <= n) return h.slice();
  return h.slice(h.length - n);
}

export function set_summary(symbol_id: number, entry: SummaryEntry): void {
  summaries.set(symbol_id, entry);
  notify(symbol_id);
}

export function get_summary(symbol_id: number): SummaryEntry | undefined {
  return summaries.get(symbol_id);
}

export function use_summary(symbol_id: number): SummaryEntry | undefined {
  return useSyncExternalStore(
    (l) => subscribe_price(symbol_id, l),
    () => summaries.get(symbol_id),
  );
}

export interface FeedStats {
  total_ticks: number;
  last_update_at: number;
}

// Cached snapshot — useSyncExternalStore compares with Object.is, so a fresh
// object literal on every call would trigger an infinite re-render loop.
let cached_stats: FeedStats = { total_ticks: 0, last_update_at: 0 };

function get_stats_snapshot(): FeedStats {
  if (
    cached_stats.total_ticks !== total_ticks ||
    cached_stats.last_update_at !== last_update_at
  ) {
    cached_stats = { total_ticks, last_update_at };
  }
  return cached_stats;
}

export function use_feed_stats(): FeedStats {
  return useSyncExternalStore(subscribe_any, get_stats_snapshot);
}

export function reset_for_tests(): void {
  state.clear();
  symbol_listeners.clear();
  global_listeners.clear();
  history.clear();
  history_version.clear();
  summaries.clear();
  total_ticks = 0;
  last_update_at = 0;
  cached_stats = { total_ticks: 0, last_update_at: 0 };
}
