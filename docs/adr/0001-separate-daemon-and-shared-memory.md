# ADR-0001: Separate C++ daemon with shared-memory tick stream

- Status: Accepted
- Date: 2026-05-03

## Context

The renderer needs to display live stock prices, and the eventual real feed
will run somewhere in the 1k–10k+ messages/sec range. The application is an
Electron app with a React renderer; the question is where the price-data
producer lives and how its output reaches the UI.

Three plausible shapes:

1. **In-process Node consumer.** Node speaks directly to a market-data
   provider (REST or WebSocket), pushes updates to the renderer.
2. **Native N-API addon in Electron's main process.** A C++ module loaded
   inside Electron consumes the feed and exposes ticks to JS. One process,
   one address space.
3. **Separate C++ daemon process + shared memory.** A standalone binary
   owns the feed connection and writes ticks into a named shared-memory
   ring buffer; Electron reads from the ring through a small N-API binding.

## Decision

We picked option 3.

## Consequences

### Why this won

- **Lifetime independence.** The feed connection survives renderer reloads,
  HMR, and crashes in the JS layer. Reconnect/backoff behaviour belongs in
  C++ where it can run with predictable timing.
- **Latency budget.** A shared-memory SPSC ring is the lowest-overhead IPC
  available short of running in the same thread. The producer never traps
  into the kernel on the hot path; the consumer pays an `acquire` load
  per drain.
- **Tooling separation.** The daemon can be profiled, traced, and
  replaced (`shm-smoke` tool, future replay binary) without touching the
  Electron stack. Replacing MockFeed with a real WebSocket client is a
  C++ refactor that doesn't perturb anything in JS.
- **Process isolation.** A bug in the feed handler or the C++ runtime
  takes the daemon down, not the UI. The supervisor restarts it.

### What this costs

- **More moving parts.** Two binaries to build, ship, and supervise. We
  pay for this with the `DaemonSupervisor` and the `extraResources`
  packaging configuration.
- **Lifecycle pitfalls.** Orphan daemon processes after a crashed dev
  loop are now possible; clean-shutdown ordering has to actually work.
  This is the #1 known gotcha and is documented in the README.
- **Cross-process invariants.** The `Tick` POD layout has to stay stable
  across the C++ producer and the C++ consumer that lives inside
  `shm_reader.node`. We defend this with a `static_assert` on
  `sizeof(Tick)` plus a versioned region name (`Local\12eTrading-ticks-v1`).
- **Code-signing surface.** Two signed executables instead of one, once
  we ship to anyone other than ourselves.

### Why not the other two

- **Option 1 (Node-only).** Adequate for the mock feed, but the
  point of this project is to learn low-latency C++ patterns. Putting the
  feed in Node also pushes hot-path work onto a single threaded event
  loop that's already responsible for IPC + Chromium message pumping.
- **Option 2 (N-API addon, in-process).** Tempting and simpler. Rejected
  because: (a) the feed dies if Electron dies, (b) Electron upgrades have
  ABI churn that affects native addons, and (c) we lose the natural
  "C++ is its own world" boundary that makes the trading logic
  testable and replay-able in isolation.

## Open questions to revisit

- Will the polling design hold up at higher tick rates, or do we need an
  eventfd / Win32 event for signal-on-write? (We can switch to a
  futex-style wait without changing the ring layout.)
- Can the renderer get away with `Float64Array` zero-copy ticks instead of
  per-tick `Object.set` in `binding.cc`? Profile before deciding.
- Where does the real-feed reconnect logic live? Likely a `IFeed`
  interface with `MockFeed` and `PolygonWsFeed` implementations behind
  it; `MarketDataService` would then own a `unique_ptr<IFeed>`.

## Reference

See [docs/architecture.md](../architecture.md) for the wiring.
