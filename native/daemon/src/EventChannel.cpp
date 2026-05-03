#include "EventChannel.h"

#include <cstdint>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
   using socket_handle_t = SOCKET;
   static constexpr socket_handle_t kInvalidSocket = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   using socket_handle_t = int;
   static constexpr socket_handle_t kInvalidSocket = -1;
#endif

namespace td {

namespace {

#ifdef _WIN32
std::once_flag g_wsa_init_once;
void ensure_wsa_init() {
    std::call_once(g_wsa_init_once, [] {
        WSADATA wsa{};
        // Failures here are essentially unrecoverable; the connect() call
        // below will surface its own error in that case.
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });
}
std::string last_socket_error() {
    return "WSA error " + std::to_string(WSAGetLastError());
}
void close_socket(socket_handle_t s) noexcept { ::closesocket(s); }
#else
void ensure_wsa_init() {}
std::string last_socket_error() {
    return std::string{std::strerror(errno)};
}
void close_socket(socket_handle_t s) noexcept { ::close(s); }
#endif

socket_handle_t to_handle(std::intptr_t s) noexcept {
    return static_cast<socket_handle_t>(s);
}

std::intptr_t from_handle(socket_handle_t s) noexcept {
    return static_cast<std::intptr_t>(s);
}

} // namespace

EventChannel::~EventChannel() {
    close();
}

bool EventChannel::connect(std::uint16_t port) {
    ensure_wsa_init();

    socket_handle_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        last_error_ = "socket(): " + last_socket_error();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        last_error_ = "connect(): " + last_socket_error();
        close_socket(sock);
        return false;
    }

    socket_ = from_handle(sock);
    last_error_.clear();
    return true;
}

bool EventChannel::send_json(const std::string& json) {
    std::lock_guard<std::mutex> guard(send_mutex_);
    if (!connected()) {
        last_error_ = "not connected";
        return false;
    }

    const std::uint32_t len = static_cast<std::uint32_t>(json.size());
    std::uint8_t        header[4];
    header[0] = static_cast<std::uint8_t>(len & 0xff);
    header[1] = static_cast<std::uint8_t>((len >> 8) & 0xff);
    header[2] = static_cast<std::uint8_t>((len >> 16) & 0xff);
    header[3] = static_cast<std::uint8_t>((len >> 24) & 0xff);

    auto send_all = [&](const char* data, std::size_t n) -> bool {
        std::size_t total = 0;
        while (total < n) {
#ifdef _WIN32
            int sent = ::send(to_handle(socket_), data + total,
                              static_cast<int>(n - total), 0);
#else
            ssize_t sent = ::send(to_handle(socket_), data + total, n - total, 0);
#endif
            if (sent <= 0) {
                last_error_ = "send(): " + last_socket_error();
                close();
                return false;
            }
            total += static_cast<std::size_t>(sent);
        }
        return true;
    };

    if (!send_all(reinterpret_cast<const char*>(header), 4)) return false;
    if (len > 0 && !send_all(json.data(), len)) return false;
    return true;
}

bool EventChannel::connected() const noexcept {
    return socket_ != from_handle(kInvalidSocket) && socket_ != -1;
}

void EventChannel::close() noexcept {
    if (connected()) {
        close_socket(to_handle(socket_));
    }
    socket_ = -1;
}

} // namespace td
