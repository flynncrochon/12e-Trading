# 12e Trading

An Electron-based stock trade viewer with a C++ market-data backend.

A standalone C++ daemon (`market-data-service`) talks to Yahoo Finance directly
and pushes everything the renderer needs into the Electron main process over
two channels:

- **Hot quote feed** — every 10 minutes for the watch list, written to a
  named shared-memory ring buffer (SPSC, lock-free).
- **History backfill + monthly summary** — one-shot at startup, pushed over
  a length-prefixed JSON event channel on TCP loopback.

Electron is a thin shim: spawn the daemon, drain the SHM ring via an N-API
addon, accept the daemon's TCP connection for events, forward both streams
to the React renderer over IPC.

## Architecture

```
                                     +---------------------------+
                                     |  Yahoo Finance            |
                                     |  v7/quote, v8/chart       |
                                     +-------------+-------------+
                                                   |
                                       libcurl (Schannel TLS on Windows)
                                                   |
+-------------------------------------+            v
|  market-data-service.exe            |  +------------------------+
|  C++ singletons / threads:          |  |  HttpClient (libcurl)  |
|    MarketDataService (producer)     |  |  YahooAuth   (crumb)   |
|    YahooFeed   <-- 10-min quotes    |--|  YahooClient (quote +  |
|    YahooHistory <-- backfill+summ.  |  |               chart)   |
|    EventChannel <-- TCP -> Electron |  +------------------------+
|    MockFeed (test fallback)         |
|    SymbolRegistry, Config, Logger   |
+--------+----------------+-----------+
         |                |
         | hot ticks      | history + summary frames
         v                v
+------------------+  +-----------------------------+
| named SHM ring   |  | TCP loopback                |
| Local\12eTrading |  | length-prefixed JSON        |
|  -ticks-v1       |  | 127.0.0.1:<ephemeral>       |
| 16 384 Tick slots|  +--------------+--------------+
+--------+---------+                 |
         |                           |
         v                           v
+------------------+      +--------------------------+
| shm_reader.node  |      | EventBridge (Node net)   |
| N-API addon      |      | parses frames, caches +  |
| poll_ticks() per |      | webContents.send(...)    |
| 16 ms            |      +--------------+-----------+
+--------+---------+                     |
         | webContents.send('ticks',...) |
         v                               v
+----------------------------------------------------------+
| React renderer                                           |
|   ticks   → price store → live row + sparkline           |
|   history → 7-day chart series                           |
|   summary → "vs ~1 month ago" % column                   |
+----------------------------------------------------------+
```

A separate control channel (named pipe) is reserved for subscribe/unsubscribe
commands and isn't wired up yet — every seeded symbol is polled
unconditionally.

## Repo layout

```
.
├── src/                    Electron + React (TypeScript)
│   ├── main/                 spawns daemon, drains SHM, accepts event TCP
│   │   ├── daemon_supervisor.ts   spawn + crash-restart for the daemon
│   │   ├── tick_bridge.ts         loads shm_reader.node, polls, IPC `ticks`
│   │   ├── event_bridge.ts        TCP listener + frame parser + caches
│   │   └── ipc_handlers.ts        symbols:list / subscribe / *:query
│   ├── preload/              context-bridge surface exposed to the renderer
│   └── renderer/             React UI — symbol list with live ticking prices
├── native/                 C++ workspace
│   ├── common/               shared SHM types: Tick, ShmRingBuffer, ShmRegion
│   ├── yahoo/                HttpClient, YahooAuth, YahooClient (lib td_yahoo)
│   ├── daemon/               market-data-service binary + EventChannel +
│   │                         YahooFeed / YahooHistory / MockFeed
│   ├── reader/               shm_reader N-API addon for the Electron main
│   ├── tools/                standalone smoke binaries (see Scripts below)
│   └── third_party/          CMake FetchContent landing zone (libcurl,
│                             nlohmann_json — fetched on first configure)
├── electron.vite.config.ts main/preload/renderer wiring
├── electron-builder.yml    packaging (extraResources for the daemon, asarUnpack for *.node)
├── scripts/                build/clean/smoke convenience scripts
├── resources/              icons + entitlements (placeholder icon for now)
└── docs/                   architecture notes + ADRs
```

## Prerequisites

- **Node.js 20** (`.nvmrc` pins this)
- **pnpm 9+** — `npm install -g pnpm`
- **CMake ≥ 3.25**
- **C++20 toolchain**
  - Windows: Visual Studio 2022 Build Tools (MSVC) + Windows 10/11 SDK
  - macOS: Xcode Command Line Tools
  - Linux: GCC 12+ or Clang 15+
- **Python 3** (only needed if cmake-js falls back to node-gyp on some ABIs)

The first `pnpm build:native` fetches and builds libcurl + nlohmann/json via
CMake FetchContent — expect a few minutes the first time, then cached.

## Getting started

```bash
pnpm install
pnpm build:native       # required — Electron loads the daemon and shm addon
pnpm dev                # launches Electron with HMR; daemon spawns on startup
```

You should see an Electron window with 16 symbols (8 US + 8 ASX). Live prices
update every ~10 minutes (Yahoo's `regularMarketPrice`); the per-symbol
sparkline backfills with a week of 1-minute closes a few seconds after launch.

## Live data feed

The daemon's `YahooFeed` polls Yahoo Finance's `v7/finance/quote` endpoint for
the entire watch list every 10 minutes. The same daemon's `YahooHistory`
fires once at startup, walks each symbol through `v8/finance/chart` for two
queries: a 7-day 1-minute series for the sparkline, and a 35-day 1-day series
for the "% vs ~1 month ago" column.

### Seeded symbols

| Exchange | Tickers |
| -------- | ------- |
| NASDAQ / NYSE | AAPL, MSFT, NVDA, TSLA, GOOG, META, AMZN, SPY |
| ASX           | BHP.AX, CBA.AX, CSL.AX, NAB.AX, WBC.AX, ANZ.AX, FMG.AX, RIO.AX |

The watch list lives in two places that must stay in sync:
[`native/daemon/src/SymbolRegistry.cpp`](native/daemon/src/SymbolRegistry.cpp)
(C++ side, used by the daemon) and
[`src/main/symbol_registry.ts`](src/main/symbol_registry.ts) (TS side, used
by the renderer's `symbol_id → ticker` lookup).

### Market hours

Yahoo returns the last close outside market hours, so quote polls succeed
24/7 but prices won't change.

| Exchange | Local hours | UTC roughly |
| -------- | ----------- | ----------- |
| NASDAQ / NYSE | Mon–Fri 09:30–16:00 ET | Mon–Fri 13:30–20:00 (DST: 14:30–21:00) |
| ASX           | Mon–Fri 10:00–16:00 AEST/AEDT | Mon–Fri 00:00–06:00 (AEDT: 23:00–05:00) |

### Yahoo caveats

Yahoo's quote/chart endpoints are unofficial and shift periodically. When
the renderer goes silent, the standalone smoke tools are how to localise the
break:

| Symptom | Try |
| ------- | --- |
| No quote updates | `pnpm smoke:yahoo-crumb` then `pnpm smoke:yahoo-quote` |
| Empty sparklines | `pnpm smoke:yahoo-chart AAPL 7 1m` |

Other things to know:

- The daemon's `YahooClient::quote` performs Yahoo's two-step crumb dance
  (GET `fc.yahoo.com` for cookies, GET `/v1/test/getcrumb` for the token)
  and refreshes the crumb on a 401 transparently. The crumb typically
  rotates every ~24 h.
- `v8/finance/chart` started 4xxing requests that omit `period2`; the C++
  client now always sends both `period1` and `period2`.
- Per-tick `volume` is the delta of `regularMarketVolume` between
  consecutive polls (clamped to 0 across day rollover). The first poll for
  each symbol emits `volume: 0`.
- `Tick.ts_ns` is the daemon's monotonic clock at observation time, not
  Yahoo's `regularMarketTime` (which can be stale in pre-market).

## Event channel

The history backfill and monthly summary are too bursty and too variable in
size for the SPSC tick ring (one batch can be thousands of points). They get
their own out-of-band channel:

- The Electron main creates a TCP listener on `127.0.0.1:0` (ephemeral port)
  before spawning the daemon.
- The daemon is launched with `--event-port=N` and connects back as a TCP
  client at startup. It reuses the same connection for every event.
- Wire format is **`[uint32 length, little-endian][N bytes UTF-8 JSON]`**.
- Two event types today:

  ```json
  {"type":"history:backfill","symbol_id":0,"points":[{"t":1730851200000,"price":189.84}, ...]}
  {"type":"summary:update","symbol_id":0,"month_ago_price":255.92,"month_ago_t":1774825200000}
  ```

- The Electron-side `EventBridge` parses frames, caches the latest of each
  per `symbol_id`, and forwards them to the renderer via the existing
  `history:backfill` and `summary:update` IPC channels.

`scripts/test-event-channel.mjs` is a self-contained verifier — it spawns the
daemon and prints one line per frame received. Useful when developing the
daemon without firing up Electron.

## Scripts

| Script              | What it does                                                      |
| ------------------- | ----------------------------------------------------------------- |
| `pnpm dev`          | electron-vite dev server with main/renderer HMR                   |
| `pnpm build`        | Bundles main, preload, and renderer into `out/`                   |
| `pnpm build:native` | Configures and builds the C++ daemon, td_yahoo, the N-API reader, and the smoke tools |
| `pnpm rebuild:native` | Wipes `native/build/` first, then rebuilds                      |
| `pnpm package`      | Builds JS + native and produces an installer via electron-builder |
| `pnpm smoke:shm`    | Runs `shm-smoke` against a live daemon (verifies the SPSC ring)   |
| `pnpm smoke:yahoo-crumb` | Runs the Yahoo crumb fetch in isolation                      |
| `pnpm smoke:yahoo-quote` | Calls `YahooClient::quote` for a symbol list, prints prices  |
| `pnpm smoke:yahoo-chart` | Calls `YahooClient::chart` and prints summary stats          |
| `pnpm typecheck`    | `tsc --noEmit` for both renderer/preload and main configs         |
| `pnpm lint`         | ESLint on the TypeScript/React sources                            |
| `pnpm format`       | Prettier write across the repo                                    |
| `pnpm clean`        | Removes `out/`, `dist/`, `release/`, `native/build/`              |

## C++ design notes

The daemon is intentionally singleton-heavy. Each singleton is the classic
Meyers form (`static T& instance()` returning a function-local static, with
copy/move deleted). The singletons:

- **`Logger`** — minimal thread-safe stderr logger with microsecond timestamps.
- **`Config`** — process-wide settings (feed kind, tick rate, poll interval,
  history lookbacks). Defaults are compiled in; `config.json` loading is
  reserved for later.
- **`SymbolRegistry`** — fixed seed list (must match `src/main/symbol_registry.ts`).
  Real subscribe/unsubscribe will arrive on the control channel later.
- **`MockFeed`** — geometric-Brownian-motion price generator. Used when
  `Config::feed_kind() == FeedKind::Mock` (test fallback).
- **`YahooFeed`** — owns one `HttpClient` + `YahooClient`. `poll()` performs
  one HTTP round-trip for the full watch list and returns one `Tick` per
  symbol that returned a price, with volume computed as the delta vs the
  prior poll.
- **`MarketDataService`** — owns the SHM region, the producer thread, and
  the ring writer. At `start()` it picks `MockFeed` or `YahooFeed` based on
  `Config::feed_kind()` and runs the corresponding loop.

`YahooHistory` (not a singleton — held by `main()` only when `--event-port`
is supplied) runs in its own background thread. It walks the watch list
sequentially, calls `YahooClient::chart` for backfill and summary, and
pushes one JSON frame per result over the `EventChannel`.

### Testability escape hatch

Singletons make it impossible to run two services or to unit-test in
isolation. Each singleton exposes a `reset_for_tests()` shim. If you find
yourself reaching for it more than once, that's the signal to refactor the
singleton into a thin accessor over an injectable implementation.

## Shared-memory protocol

Defined in [`native/common/include/Tick.h`](native/common/include/Tick.h),
[`ShmLayout.h`](native/common/include/ShmLayout.h), and
[`ShmRingBuffer.h`](native/common/include/ShmRingBuffer.h).

```cpp
struct Tick {                  // 32 bytes, trivially copyable
    uint32_t symbol_id;
    uint32_t volume;
    double   price;
    uint64_t ts_ns;            // monotonic ns since process start
    uint64_t seq;              // monotonically increasing
};
```

- **Region name (versioned):** `Local\12eTrading-ticks-v1` on Windows;
  `/12eTrading-ticks-v1` on POSIX.
- **Ring capacity:** 16384 slots (power of two so the index can be masked).
- **Concurrency:** single-producer single-consumer, lock-free, head/tail are
  `std::atomic<uint64_t>` on separate cache lines (`alignas(64)`).
- **Ordering:** producer uses `release` on its head store; consumer uses
  `acquire` on the head load.

Bumping the `Tick` shape **must** bump the version suffix in
`ShmLayout.h` (`-v1` → `-v2`). Skipping this leaves stale dev installs
silently corrupting their consumer.
