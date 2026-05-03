# 12e Trading

An Electron-based stock trade viewer.

The current data path uses a Node-side adapter in the Electron main process
that polls Yahoo Finance every 2 seconds for a seeded set of US and ASX
symbols, then forwards tick batches to a React renderer over IPC.

A second data path is also present in the source tree: a C++ daemon that
publishes ticks to a named shared-memory ring buffer and is read via an N-API
addon. That path is no longer wired into `bootstrap()` but the source is kept
intact for reference and can be re-enabled by restoring the daemon spawn in
`src/main/index.ts`.

## Architecture

```
+------------------------------+
|  market-data-service.exe     |   standalone C++ process
|  (singletons:                |   - MockFeed generates synthetic ticks
|     MarketDataService,       |   - SymbolRegistry holds the watch list
|     SymbolRegistry,          |   - Logger / Config / MockFeed
|     MockFeed,                |
|     Logger, Config)          |
+--------------+---------------+
               |  writes Tick structs
               v
+------------------------------+
|  named shared memory         |   Local\12eTrading-ticks-v1 (Windows)
|  SPSC ring buffer            |   /12eTrading-ticks-v1     (POSIX)
+--------------+---------------+
               |  reads
               v
+------------------------------+
|  shm-reader.node             |   N-API addon, loaded by Electron main
|  pollTicks(maxN) -> Tick[]   |
+--------------+---------------+
               |  IPC (webContents.send)
               v
+------------------------------+
|  React renderer              |   useTickStream -> price store -> rows
+------------------------------+
```

A separate control channel (named pipe) is reserved for subscribe/unsubscribe
commands and isn't wired up yet — only the hot tick stream flows in this
first slice.

## Repo layout

```
.
├── src/                    Electron + React (TypeScript)
│   ├── main/                 main process: spawns daemon, polls reader, IPC to renderer
│   ├── preload/              context-bridge surface exposed to the renderer
│   └── renderer/             React UI — symbol list with live ticking prices
├── native/                 C++ workspace
│   ├── common/               cross-process types: Tick, ShmRingBuffer, ShmLayout, ShmRegion
│   ├── daemon/               market-data-service binary (singletons + producer loop)
│   ├── reader/               shm-reader N-API addon for the Electron main process
│   ├── tools/shm-smoke/      standalone consumer for verifying the daemon without Electron
│   └── third_party/          CMake FetchContent landing zone (spdlog, fmt)
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

## Getting started

```bash
pnpm install
pnpm dev            # launches Electron with HMR; live Yahoo feed starts automatically
```

You should see an Electron window with 16 symbols (8 US + 8 ASX) whose
prices update every ~2 seconds. The status badge in the top-right shows
`live · Xs ago` while polling is healthy.

`pnpm build:native` is only required if you intend to re-enable the C++
daemon path described under [Optional: C++ daemon path](#optional-c-daemon-path).

## Live data feed

The default data source is **Yahoo Finance** via the `yahoo-finance2` npm
package. It is polled every 2 seconds in one batched `quote()` request and
the results are converted to the same `IpcTick` shape the renderer already
consumes — no renderer changes were needed to swap data sources.

### Seeded symbols

| Exchange | Tickers |
| -------- | ------- |
| NASDAQ / NYSE | AAPL, MSFT, NVDA, TSLA, GOOG, META, AMZN, SPY |
| ASX           | BHP.AX, CBA.AX, CSL.AX, NAB.AX, WBC.AX, ANZ.AX, FMG.AX, RIO.AX |

To change the watch list, edit `SEED_SYMBOLS` in
[`src/main/symbol_registry.ts`](src/main/symbol_registry.ts) and restart
`pnpm dev`. Use Yahoo's ticker convention (e.g. `.AX` suffix for ASX,
`.L` for LSE, `.TO` for TSX).

### Status badge states

| State | Meaning |
| ----- | ------- |
| `live · Xs ago` (green) | Last poll succeeded `X` seconds ago. |
| `retrying · last ok Xs ago` (amber) | One or two consecutive polls failed; backoff in progress. |
| `feed error: …` (red) | Three or more consecutive failures. The error message comes from the Yahoo client. |
| `starting…` (grey) | First poll has not completed yet. |

### Market hours

Yahoo returns the last close outside market hours, so the badge stays green
24/7 but prices won't change.

| Exchange | Local hours | UTC roughly |
| -------- | ----------- | ----------- |
| NASDAQ / NYSE | Mon–Fri 09:30–16:00 ET | Mon–Fri 13:30–20:00 (DST: 14:30–21:00) |
| ASX           | Mon–Fri 10:00–16:00 AEST/AEDT | Mon–Fri 00:00–06:00 (AEDT: 23:00–05:00) |

### Caveats

- `yahoo-finance2` is an unofficial scraper. Yahoo can break the endpoint
  without notice. The status badge will show `feed error: …` if that
  happens — typically the fix is a `pnpm up yahoo-finance2`.
- Per-tick `volume` is computed as the delta of `regularMarketVolume`
  between consecutive polls (clamped to 0 across day rollover). The first
  poll for each symbol emits `volume: 0`.
- `ts_ns` reflects the time we observed the price, not Yahoo's
  `regularMarketTime` (which can be stale in pre-market).

## Optional: C++ daemon path

The original C++ market-data daemon and N-API reader still live in
[`native/`](native/) and remain buildable:

```bash
pnpm build:native
```

It is no longer wired into `bootstrap()`. To swap back, restore the
`DaemonSupervisor` + `attach_to_daemon` block in `src/main/index.ts` and
remove the `start_yahoo_feed` call. See git history for the previous version.

## Scripts

| Script              | What it does                                                      |
| ------------------- | ----------------------------------------------------------------- |
| `pnpm dev`          | electron-vite dev server with main/renderer HMR                   |
| `pnpm build`        | Bundles main, preload, and renderer into `out/`                   |
| `pnpm build:native` | Configures and builds the C++ daemon and the N-API reader addon   |
| `pnpm rebuild:native` | Wipes `native/build/` first, then rebuilds                      |
| `pnpm package`      | Builds JS + native and produces an installer via electron-builder |
| `pnpm smoke:shm`    | Runs the standalone `shm-smoke` consumer against a live daemon    |
| `pnpm typecheck`    | `tsc --noEmit` for both renderer/preload and main configs         |
| `pnpm lint`         | ESLint on the TypeScript/React sources                            |
| `pnpm format`       | Prettier write across the repo                                    |
| `pnpm clean`        | Removes `out/`, `dist/`, `release/`, `native/build/`              |

## C++ design notes

The daemon is intentionally singleton-heavy. Each singleton is the classic
Meyers form (`static T& instance()` returning a function-local static, with
copy/move deleted). The five singletons:

- **`Logger`** — wraps spdlog with a rotating file sink + stderr sink.
- **`Config`** — process-wide settings (tick rate, ring sizing). Loads
  `config.json` next to the binary if present, otherwise compiled defaults.
- **`SymbolRegistry`** — fixed seed list for now (`AAPL MSFT NVDA TSLA GOOG META AMZN SPY`).
  Real subscribe/unsubscribe will arrive on the control channel later.
- **`MockFeed`** — geometric-Brownian-motion price generator. Produces a tick
  per symbol per cycle at the rate set in `Config`.
- **`MarketDataService`** — owns the producer thread and the ring-buffer writer.
  Drains `MockFeed → Tick → ring`.

### Testability escape hatch

Singletons make it impossible to run two services or to unit-test in isolation.
Each singleton exposes a `reset_for_tests()` shim. If you find yourself
reaching for it more than once, that's the signal to refactor the singleton
into a thin accessor over an injectable implementation.

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