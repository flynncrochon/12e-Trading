#include "ShmRegion.h"

#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <cerrno>
#  include <cstring>
#endif

namespace td::shm {

namespace {

#ifdef _WIN32

void* win_create_or_open(const char* name, std::size_t size, void** handle_out) {
    const DWORD size_high = static_cast<DWORD>((static_cast<uint64_t>(size) >> 32) & 0xffffffffu);
    const DWORD size_low  = static_cast<DWORD>(size & 0xffffffffu);

    HANDLE h = ::CreateFileMappingA(
        INVALID_HANDLE_VALUE,   // backed by the page file
        nullptr,
        PAGE_READWRITE,
        size_high,
        size_low,
        name);
    if (h == nullptr) {
        throw std::runtime_error("CreateFileMappingA failed: " + std::to_string(::GetLastError()));
    }

    void* p = ::MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (p == nullptr) {
        const DWORD err = ::GetLastError();
        ::CloseHandle(h);
        throw std::runtime_error("MapViewOfFile failed: " + std::to_string(err));
    }

    *handle_out = h;
    return p;
}

void* win_open_existing(const char* name, std::size_t size, void** handle_out) {
    HANDLE h = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (h == nullptr) {
        throw std::runtime_error(
            "OpenFileMappingA failed: " + std::to_string(::GetLastError()));
    }

    void* p = ::MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (p == nullptr) {
        const DWORD err = ::GetLastError();
        ::CloseHandle(h);
        throw std::runtime_error("MapViewOfFile failed: " + std::to_string(err));
    }

    *handle_out = h;
    return p;
}

#else // POSIX

void* posix_create_or_open(const char* name, std::size_t size, int* fd_out) {
    int fd = ::shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        throw std::runtime_error(std::string("shm_open failed: ") + std::strerror(errno));
    }
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const int err = errno;
        ::close(fd);
        throw std::runtime_error(std::string("ftruncate failed: ") + std::strerror(err));
    }
    void* p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        const int err = errno;
        ::close(fd);
        throw std::runtime_error(std::string("mmap failed: ") + std::strerror(err));
    }
    *fd_out = fd;
    return p;
}

void* posix_open_existing(const char* name, std::size_t size, int* fd_out) {
    int fd = ::shm_open(name, O_RDWR, 0600);
    if (fd < 0) {
        throw std::runtime_error(std::string("shm_open(existing) failed: ") + std::strerror(errno));
    }
    void* p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        const int err = errno;
        ::close(fd);
        throw std::runtime_error(std::string("mmap failed: ") + std::strerror(err));
    }
    *fd_out = fd;
    return p;
}

#endif

} // namespace

Region::~Region() { close(); }

Region::Region(Region&& other) noexcept
    : data_(other.data_), size_(other.size_)
#ifdef _WIN32
    , handle_(other.handle_)
#else
    , fd_(other.fd_), posix_name_(std::move(other.posix_name_)), owns_unlink_(other.owns_unlink_)
#endif
{
    other.data_ = nullptr;
    other.size_ = 0;
#ifdef _WIN32
    other.handle_ = nullptr;
#else
    other.fd_ = -1;
    other.owns_unlink_ = false;
#endif
}

Region& Region::operator=(Region&& other) noexcept {
    if (this == &other) return *this;
    close();
    data_ = other.data_;
    size_ = other.size_;
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    fd_ = other.fd_;
    posix_name_ = std::move(other.posix_name_);
    owns_unlink_ = other.owns_unlink_;
    other.fd_ = -1;
    other.owns_unlink_ = false;
#endif
    other.data_ = nullptr;
    other.size_ = 0;
    return *this;
}

Region Region::create_or_open(const char* name, std::size_t size) {
    Region r;
    r.size_ = size;
#ifdef _WIN32
    r.data_ = win_create_or_open(name, size, &r.handle_);
#else
    r.data_ = posix_create_or_open(name, size, &r.fd_);
    r.posix_name_ = name;
    r.owns_unlink_ = true;
#endif
    return r;
}

Region Region::open_existing(const char* name, std::size_t size) {
    Region r;
    r.size_ = size;
#ifdef _WIN32
    r.data_ = win_open_existing(name, size, &r.handle_);
#else
    r.data_ = posix_open_existing(name, size, &r.fd_);
    r.posix_name_ = name;
    r.owns_unlink_ = false;
#endif
    return r;
}

void Region::close() noexcept {
#ifdef _WIN32
    if (data_) {
        ::UnmapViewOfFile(data_);
        data_ = nullptr;
    }
    if (handle_) {
        ::CloseHandle(handle_);
        handle_ = nullptr;
    }
#else
    if (data_ && size_ > 0) {
        ::munmap(data_, size_);
        data_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (owns_unlink_ && !posix_name_.empty()) {
        ::shm_unlink(posix_name_.c_str());
        owns_unlink_ = false;
    }
    posix_name_.clear();
#endif
    size_ = 0;
}

} // namespace td::shm
