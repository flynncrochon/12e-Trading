import { useMemo, useState, type ChangeEvent, type MouseEvent } from 'react';
import { use_news_items, type NewsItem } from '../store/news';

const MS_PER_HOUR = 60 * 60 * 1000;
const MS_PER_DAY  = 24 * MS_PER_HOUR;

type PresetId = '24h' | '7d' | '30d' | '90d' | 'all' | 'custom';

const PRESETS: ReadonlyArray<{ id: Exclude<PresetId, 'custom'>; label: string; days: number | null }> = [
  { id: '24h', label: '24h', days: 1   },
  { id: '7d',  label: '7d',  days: 7   },
  { id: '30d', label: '30d', days: 30  },
  { id: '90d', label: '90d', days: 90  },
  { id: 'all', label: 'All', days: null },
];

// Words / phrases that signal genuinely-bad news in a headline. Curated
// against ~100 real Yahoo headlines from current day_losers — terms that
// appear in clearly-negative stories (price plunges, lawsuits, layoffs,
// guidance cuts) are in; terms that swing both ways ("falls", "drops",
// "cut" alone, "weakness") are deliberately out, since black-swan mode
// errs on precision. A missed item just falls back to the toggle-off
// view, so it's safer to under-match than to over-match.
//
// Note: there's no separate "routine" exclusion list. A headline like
// "stock plunges after Q1 earnings miss" used to get dropped because of
// "earnings"; now it correctly passes via "plunges". Earnings/guidance
// reports without a negative signal are still excluded (just by failing
// this list, not by a separate gate).
const NEGATIVE_SIGNAL_KEYWORDS = [
  // Price-action language
  'plunge', 'plunges', 'plunged',
  'crash', 'crashes', 'crashed',
  'tumble', 'tumbles', 'tumbled',
  'tank', 'tanks', 'tanked',
  'slump', 'slumps', 'slumped',
  'sink', 'sinks', 'sinking',
  'slide', 'slides', 'sliding', 'slid',
  'dive', 'dives', 'dived',
  'rout', 'sell-off', 'selloff',
  'obliterate', 'obliterated', 'obliterating',

  // Action verbs (negative)
  'slash', 'slashes', 'slashed', 'slashing',
  'cuts', 'cutting',
  'stumble', 'stumbles', 'stumbled', 'stumbling',
  'downgrade', 'downgraded', 'downgrades',

  // Crisis / scandal
  'fraud', 'scandal', 'bribery', 'corruption',
  'embezzle', 'embezzled', 'embezzlement',
  'allegation', 'allegations', 'accused', 'accusation',

  // Legal / regulatory
  'lawsuit', 'sued', 'sues', 'suing',
  'indicted', 'indictment',
  'investigation', 'investigating',
  'probe', 'probed', 'probes',
  'subpoena', 'antitrust',
  'fined', 'penalty', 'penalties',
  'sanction', 'sanctions', 'sanctioned',
  'banned',

  // Cybersecurity
  'breach', 'breached', 'breaches',
  'hack', 'hacked', 'hacker', 'hackers',
  'ransomware', 'cyberattack', 'cyber attack',
  'data leak', 'leaked',

  // Operational disasters
  'recall', 'recalls', 'recalled',
  'defect', 'defective', 'malfunction',
  'outage', 'outages',
  'halts', 'halted',
  'suspends', 'suspended', 'suspension',
  'delays', 'delayed',
  'withdraws', 'withdrawn',

  // HR turmoil
  'fired', 'firing',
  'resigns', 'resigned', 'resignation',
  'ousted', 'ouster',
  'layoff', 'layoffs', 'lay-off', 'lay off',
  'job cuts',
  'sacked',
  'step down', 'steps down', 'stepped down',

  // Financial distress
  'bankruptcy', 'bankrupt',
  'insolvency', 'insolvent',
  'default', 'defaulted',
  'restructuring',

  // Warnings / negative outlook
  'warning', 'warns', 'warned',
  'profit warning',
  'slowdown',
  'crisis',
  'shortage',
  'struggling', 'troubles', 'troubled',

  // Negative outcomes
  'failed', 'failure',
  'rejected', 'rejection',

  // Severe events
  'dies', 'dead', 'killed', 'death', 'fatal',
  'explosion',
  'collapse', 'collapses', 'collapsed', 'collapsing',
];

const NEGATIVE_REGEX = new RegExp(
  `\\b(?:${NEGATIVE_SIGNAL_KEYWORDS.map((k) =>
    k
      .replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
      .replace(/\s+/g, '\\s+'),
  ).join('|')})\\b`,
  'i',
);

function has_negative_signal(title: string): boolean {
  return NEGATIVE_REGEX.test(title);
}

function start_of_today(): Date {
  const d = new Date();
  d.setHours(0, 0, 0, 0);
  return d;
}

function days_before(d: Date, n: number): Date {
  return new Date(d.getTime() - n * MS_PER_DAY);
}

function format_iso_date(d: Date): string {
  const yyyy = d.getFullYear();
  const mm = String(d.getMonth() + 1).padStart(2, '0');
  const dd = String(d.getDate()).padStart(2, '0');
  return `${yyyy}-${mm}-${dd}`;
}

function parse_iso_date(s: string): Date | null {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s);
  if (!m) return null;
  const d = new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]));
  d.setHours(0, 0, 0, 0);
  return d;
}

function format_age(published_t: number): string {
  const delta = Date.now() - published_t;
  if (delta < MS_PER_HOUR) {
    const m = Math.max(1, Math.round(delta / 60_000));
    return `${m}m ago`;
  }
  if (delta < MS_PER_DAY) {
    return `${Math.round(delta / MS_PER_HOUR)}h ago`;
  }
  return `${Math.round(delta / MS_PER_DAY)}d ago`;
}

function format_pct(p: number): string {
  const sign = p >= 0 ? '+' : '';
  return `${sign}${p.toFixed(2)}%`;
}

/**
 * Renders news for stocks that Yahoo's `day_losers` screener has flagged as
 * negatively impacted (US + AU). The ticker universe is *not* limited to
 * the user's watchlist — the daemon discovers the negatively-impacted
 * universe at startup. Items are filtered to those with negative
 * market-adjusted impact at headline time, within a user-selected
 * publish-date window, and sorted most-negative first.
 */
export function NewsTab() {
  const items = use_news_items();

  const today = useMemo(start_of_today, []);
  const [from_date, set_from_date] = useState<Date>(() => days_before(today, 90));
  const [to_date,   set_to_date]   = useState<Date>(() => today);
  const [preset,    set_preset]    = useState<PresetId>('90d');
  const [black_swan,    set_black_swan]    = useState<boolean>(true);
  const [min_drop_pct,  set_min_drop_pct]  = useState<number>(3);

  // Window is half-open: [from_ms, to_ms). to_ms is the start of the day
  // *after* to_date so headlines on to_date itself are still included.
  const window_ms = useMemo(() => {
    const from_ms = from_date.getTime();
    const to_ms   = to_date.getTime() + MS_PER_DAY;
    return { from_ms, to_ms };
  }, [from_date, to_date]);

  // Items that pass the date + negative-impact filter, before black-swan
  // tightening. Used to count how many we hide so the empty state can say
  // "X hidden by black-swan filter".
  const in_window_negative = useMemo(() => {
    return items.filter((it) =>
      it.adjusted_pct < 0 &&
      it.published_t >= window_ms.from_ms &&
      it.published_t < window_ms.to_ms,
    );
  }, [items, window_ms]);

  const negative = useMemo(() => {
    let pool = in_window_negative;
    if (black_swan) {
      const ceiling = -Math.abs(min_drop_pct);
      pool = pool.filter(
        (it) => it.adjusted_pct <= ceiling && has_negative_signal(it.title),
      );
    }
    return pool.slice().sort((a, b) => a.adjusted_pct - b.adjusted_pct);
  }, [in_window_negative, black_swan, min_drop_pct]);

  // Collapse to one row per ticker — the worst-scored headline wins.
  // `negative` is already sorted ascending by adjusted_pct, so the first
  // sighting of each ticker is the most-impactful headline for that stock.
  const top_per_stock = useMemo(() => {
    const seen = new Set<string>();
    const out: NewsItem[] = [];
    for (const it of negative) {
      if (seen.has(it.ticker)) continue;
      seen.add(it.ticker);
      out.push(it);
    }
    return out;
  }, [negative]);

  const hidden_by_black_swan = black_swan
    ? in_window_negative.length - negative.length
    : 0;

  const apply_preset = (id: Exclude<PresetId, 'custom'>) => {
    set_preset(id);
    set_to_date(today);
    const cfg = PRESETS.find((p) => p.id === id);
    if (!cfg) return;
    if (cfg.days === null) {
      // "All" — anchor far enough back that anything in the cache passes.
      set_from_date(days_before(today, 365 * 5));
    } else {
      set_from_date(days_before(today, cfg.days));
    }
  };

  const on_from_change = (e: ChangeEvent<HTMLInputElement>) => {
    const d = parse_iso_date(e.target.value);
    if (d) {
      set_from_date(d);
      set_preset('custom');
    }
  };

  const on_to_change = (e: ChangeEvent<HTMLInputElement>) => {
    const d = parse_iso_date(e.target.value);
    if (d) {
      set_to_date(d);
      set_preset('custom');
    }
  };

  const handle_click = (e: MouseEvent, item: NewsItem) => {
    e.preventDefault();
    if (item.link) window.trading.open_external(item.link);
  };

  const total_negative_in_cache = useMemo(
    () => items.filter((it) => it.adjusted_pct < 0).length,
    [items],
  );

  return (
    <div className="news-tab">
      <div className="news-controls">
        <div className="news-presets" role="group" aria-label="Timeframe preset">
          {PRESETS.map((p) => (
            <button
              key={p.id}
              type="button"
              className={`news-chip ${preset === p.id ? 'active' : ''}`}
              onClick={() => apply_preset(p.id)}
            >
              {p.label}
            </button>
          ))}
        </div>
        <label className="news-range-field">
          From
          <input
            type="date"
            value={format_iso_date(from_date)}
            onChange={on_from_change}
            max={format_iso_date(to_date)}
          />
        </label>
        <label className="news-range-field">
          To
          <input
            type="date"
            value={format_iso_date(to_date)}
            onChange={on_to_change}
            min={format_iso_date(from_date)}
          />
        </label>
        <span className="news-count">
          {items.length === 0
            ? 'fetching…'
            : `${top_per_stock.length} ${top_per_stock.length === 1 ? 'stock' : 'stocks'}`
              + ` · ${negative.length} headlines`}
        </span>
      </div>

      <div className="news-controls news-controls-secondary">
        <label className="news-toggle">
          <input
            type="checkbox"
            checked={black_swan}
            onChange={(e) => set_black_swan(e.target.checked)}
          />
          Black swan only
          <span className="news-toggle-hint">
            requires negative-signal language in the headline (plunge, lawsuit,
            slashed, recall, layoff, etc.)
          </span>
        </label>
        <label className="news-range-field">
          Min drop %
          <input
            type="number"
            min={0}
            max={50}
            step={0.5}
            value={min_drop_pct}
            disabled={!black_swan}
            onChange={(e) => {
              const v = Number(e.target.value);
              if (Number.isFinite(v) && v >= 0) set_min_drop_pct(v);
            }}
          />
        </label>
      </div>

      {items.length === 0 ? (
        <div className="tab-empty">Fetching news…</div>
      ) : negative.length === 0 ? (
        <div className="tab-empty">
          {black_swan && hidden_by_black_swan > 0 ? (
            <>
              No black-swan items in this window. {hidden_by_black_swan} routine
              negative {hidden_by_black_swan === 1 ? 'item' : 'items'} hidden —
              uncheck "Black swan only" or lower the drop floor to see them.
            </>
          ) : (
            <>
              No negative-impact news in this window.
              {total_negative_in_cache > 0 && (
                <> Try widening the range — {total_negative_in_cache} negative items elsewhere in the cache.</>
              )}
            </>
          )}
        </div>
      ) : (
        <div className="news-list">
          {top_per_stock.map((item) => (
            <a
              key={item.news_id}
              className="news-item"
              href={item.link || '#'}
              onClick={(e) => handle_click(e, item)}
              rel="noreferrer noopener"
            >
              <div className="news-impact">
                <span className="news-ticker">{item.ticker}</span>
                <span className="news-pct">{format_pct(item.daily_change_pct)}</span>
                <span className="news-impact-sub">today</span>
              </div>
              <div className="news-body">
                {item.short_name && <div className="news-shortname">{item.short_name}</div>}
                <div className="news-title">{item.title}</div>
                <div className="news-meta">
                  <span>{item.publisher || 'unknown'}</span>
                  <span aria-hidden="true">·</span>
                  <span>{format_age(item.published_t)}</span>
                  <span aria-hidden="true">·</span>
                  <span>
                    news impact {format_pct(item.adjusted_pct)} vs {item.benchmark}
                  </span>
                </div>
              </div>
            </a>
          ))}
        </div>
      )}
    </div>
  );
}
