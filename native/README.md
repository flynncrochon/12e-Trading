# native/

C++ workspace for the 12e Trading market-data backend. Three build targets
plus shared headers:

| Target                    | What it is                                              |
| ------------------------- | ------------------------------------------------------- |
| `td_common` (static lib)  | `Tick`, `ShmLayout`, `ShmRingBuffer`, `ShmRegion`       |
| `market-data-service`     | The daemon binary (singletons + producer thread)        |
| `shm_reader.node`         | N-API addon loaded by Electron's main process           |
| `shm-smoke`               | Standalone consumer for verifying the daemon            |

## Layout

```
native/
├── common/
│   ├── include/   Tick.h, ShmLayout.h, ShmRingBuffer.h, ShmRegion.h
│   └── src/       ShmRegion.cpp (Win32 / POSIX wrapper)
├── daemon/        Singletons (Logger, Config, SymbolRegistry, YahooFeed,
│                  YahooHistory, MarketDataService) + main.cpp
├── reader/        ShmReader.cpp + N-API binding (binding.cc)
├── tools/
│   └── shm-smoke/ Standalone consumer
└── third_party/   CMake FetchContent landing zone (currently empty)
```

## Build

The convenience entry point is `pnpm build:native` from the repo root, but
the underlying commands are:

```bash
# daemon + smoke tool
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release

# N-API reader addon (built separately by cmake-js because it needs
# Node-specific include paths)
npx cmake-js compile --directory native/reader --config Release
```

The `.node` artifact lands in `native/reader/build/Release/shm_reader.node`.
The build script copies it to a stable location the Electron main process
can `require()`.

## Concurrency invariants

- Single producer (the daemon) writes via `RingWriter`.
- Single consumer (either the N-API reader or `shm-smoke`) reads via
  `RingReader`.
- Head and tail counters live on dedicated cache lines (`alignas(64)`).
- Producer publishes a slot with a `release` store on `head`; consumer reads
  with an `acquire` load on `head`.
- The ring is sized to a power of two; the slot index is `idx & kRingMask`.

Running two consumers against the same region is undefined behaviour: the
ring is SPSC by construction.

## Debugging

- The daemon logs to stderr with microsecond timestamps. Pipe it through any
  log viewer or just run it in a console.
- `shm-smoke` is the fastest way to confirm the daemon is publishing without
  Electron in the loop.
- To inspect the region from outside, use Process Explorer (Windows) or
  `lsof` (POSIX) on the named handle.
