#pragma once

#include <cstddef>
#include <string>

namespace td::shm {

// Cross-platform wrapper around a named shared-memory region.
// Windows: CreateFileMapping / MapViewOfFile.
// POSIX:   shm_open / mmap.
//
// `create_or_open` is what the daemon uses (idempotent). `open_existing` is
// what the reader uses; it fails if the region does not yet exist.
class Region {
public:
    Region() = default;
    ~Region();

    Region(Region&& other) noexcept;
    Region& operator=(Region&& other) noexcept;
    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;

    static Region create_or_open(const char* name, std::size_t size);
    static Region open_existing(const char* name, std::size_t size);

    void*       data() noexcept { return data_; }
    const void* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool        valid() const noexcept { return data_ != nullptr; }

private:
    void close() noexcept;

    void*       data_ = nullptr;
    std::size_t size_ = 0;

#ifdef _WIN32
    void* handle_ = nullptr;   // HANDLE returned by CreateFileMapping
#else
    int         fd_ = -1;
    std::string posix_name_;   // remembered so we can shm_unlink on close (creator only)
    bool        owns_unlink_ = false;
#endif
};

} // namespace td::shm
