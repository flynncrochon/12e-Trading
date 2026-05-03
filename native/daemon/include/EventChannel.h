#pragma once

#include <cstdint>
#include <string>

namespace td {

// Length-prefixed JSON event channel. Out-of-band relative to the SPSC tick
// ring — used by the daemon to push variable-size, low-frequency events
// (history backfill, monthly summary) up to the Electron main process.
//
// Wire format: [uint32 length, little-endian][N bytes of UTF-8 JSON]
//
// Connects as a TCP client to 127.0.0.1:<port> at startup. The Electron main
// owns the listener (created on an ephemeral port and passed to the daemon
// via --event-port). One connection at a time; not thread-safe — callers
// must serialize send_json() calls.
class EventChannel {
public:
    EventChannel()  = default;
    ~EventChannel();

    EventChannel(const EventChannel&)            = delete;
    EventChannel& operator=(const EventChannel&) = delete;

    // Connects to 127.0.0.1:<port>. Returns false on failure; last_error()
    // describes the cause.
    bool connect(std::uint16_t port);

    // Sends one frame. Returns false on first send failure (subsequent calls
    // also fail until reconnect/close).
    bool send_json(const std::string& json);

    bool connected() const noexcept;
    void close() noexcept;

    const std::string& last_error() const noexcept { return last_error_; }

private:
    // Holds either an int fd (POSIX) or a SOCKET (Windows uintptr_t). Sized
    // wide enough for both; -1 means "no socket" on POSIX, ~0 on Windows.
    std::intptr_t socket_ = -1;
    std::string   last_error_;
};

} // namespace td
