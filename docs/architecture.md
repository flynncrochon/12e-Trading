# Architecture

A walkthrough of the data path, from synthetic price generation to a flickering
React row, and the boundaries between the four moving parts.

## Boxes

```
+--------------------------------------------------------------+
|  market-data-service.exe   (separate process, C++)           |
|                                                              |
|  +----------+    +----------------------+                    |
|  |  Config  |    |   SymbolRegistry     |                    |
|  | (singleton) | (singleton, seed list) |                    |
|  +----------+    +----------------------+                    |
|       |                  |                                   |
|       v                  v                                   |
|  +----------+     +-----------------+   step()  +---------+  |
|  |  Logger  | <-- | MockFeed         | -------> |  Tick   |  |
|  |          |     | (singleton, GBM) |          +---------+  |
|  +----------+     +-----------------+               |        |
|                          ^                          v        |
|                          |  drain loop          try_push()   |
|                  +-----------------------+                   |
|                  |  MarketDataService    |                   |
|                  |  (singleton, owns     |                   |
|                  |   producer thread &   |                   |
|                  |   ring writer)        |                   |
|                  +-----------------------+                   |
+----------------------|---------------------------------------+
                       |
                       v   release-store on head
+--------------------------------------------------------------+
|         named shared memory  Local\12eTrading-ticks-v1       |
|                                                              |
|   header (magic, version, capacity, head, tail)              |
|   + Tick[16384] ring                                         |
+--------------------------------------------------------------+
                       |
                       |   acquire-load on head, release-store on tail
                       v
+--------------------------------------------------------------+
|  Electron main process (Node.js)                             |
|                                                              |
|  +----------------------+   +-------------------+            |
|  | DaemonSupervisor     |   | shm_reader.node   |            |
|  | spawn / restart /    |   | open / pollTicks  |            |
|  | graceful stop        |   |                   |            |
|  +----------------------+   +---------+---------+            |
|                                       |                      |
|                                       v setInterval(16ms)    |
|                          +---------------------+             |
|                          | tick-bridge          |            |
|                          | webContents.send     |            |
|                          +---------------------+             |
+----------------------|---------------------------------------+
                       |  IPC
                       v
+--------------------------------------------------------------+
|  Renderer (React + TypeScript)                               |
|                                                              |
|  preload exposeInMainWorld('trading')                        |
|     -> window.trading.onTicks(handler)                       |
|     -> useTickStream() -> ingestTicks() -> price store       |
|     -> usePrice(id) (useSyncExternalStore)                   |
|     -> PriceRow flashes green/red on diff                    |
+--------------------------------------------------------------+
```

## Tick lifecycle

1. `MarketDataService::run_loop` wakes every `Config::loop_slice()`
   (100 ms by default).
2. For each symbol it calls `MockFeed::step` to advance one geometric
   Brownian motion step and produce a `Tick`.
3. `RingWriter::try_push` writes the tick at `head & kRingMask` and stores
   `head + 1` with `memory_order_release`. If the ring is full (i.e. the
   consumer fell hopelessly behind), the tick is dropped and the
   `dropped_` counter is bumped.
4. The Electron main process polls every 16 ms via the N-API addon. The
   addon's `pollTicks(maxN)` calls `RingReader::pop_batch`, which
   acquire-loads `head`, copies up to `maxN` ticks, and release-stores
   `tail + n`.
5. Main process forwards the batch as a JS array via
   `webContents.send('ticks', batch)`.
6. The preload `contextBridge` exposes the IPC channel as
   `window.trading.onTicks`. The renderer's `useTickStream` hook funnels
   batches into `ingestTicks` in the price store.
7. `ingestTicks` writes each new price to the in-memory `Map` and
   notifies only the listeners for the affected symbols
   (`useSyncExternalStore`). Untouched rows do not re-render.
8. `PriceRow` runs its diff effect against `lastPrice`, sets a flash
   class for 250 ms, and updates the displayed price.

## Tick struct

| Field       | Type       | Meaning                                            |
| ----------- | ---------- | -------------------------------------------------- |
| `symbol_id` | `uint32_t` | Index into `SymbolRegistry::entries()`             |
| `volume`    | `uint32_t` | Shares traded for this print                       |
| `price`     | `double`   | Last trade price                                   |
| `ts_ns`     | `uint64_t` | Monotonic nanoseconds since the daemon started     |
| `seq`       | `uint64_t` | Monotonically increasing per-tick sequence number  |

Total: 32 bytes, no padding. `static_assert`s in `Tick.h` guard the layout.

## Why a separate process

See [adr/0001-separate-daemon-and-shared-memory.md](adr/0001-separate-daemon-and-shared-memory.md).

## Where the IPC happens

There are three IPC layers stacked on this path:

1. **C++ ↔ C++ across processes:** lock-free SPSC ring in shared memory.
   This is the only one on the hot tick path; everything else is best-effort.
2. **C++ ↔ Node within the main process:** N-API function call. Same
   address space; effectively a function call plus a JS object
   construction loop.
3. **Main ↔ renderer:** Chromium IPC (`webContents.send`). Higher latency
   than the previous two but only fires once per polling slice (16 ms),
   carrying a batch.

The whole point of the layout is that the producer never crosses an OS
boundary on the hot path: from `MarketDataService::run_loop` to the
N-API binding is all writes and reads in a shared mapped region.
