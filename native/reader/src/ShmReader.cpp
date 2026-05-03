#include "ShmReader.h"

#include "ShmLayout.h"

namespace td {

ShmReader::ShmReader() = default;

bool ShmReader::open() {
    if (region_.valid()) return true;
    try {
        region_ = shm::Region::open_existing(shm::kShmName, shm::kRegionSize);
    } catch (...) {
        return false;
    }
    auto* header = static_cast<shm::Header*>(region_.data());
    if (header->magic != shm::kMagic || header->version != shm::kVersion) {
        region_ = shm::Region{};
        return false;
    }
    reader_ = shm::RingReader{header, shm::slots_of(region_.data())};
    return true;
}

void ShmReader::close() {
    region_ = shm::Region{};
}

std::size_t ShmReader::poll(Tick* out, std::size_t max_n) {
    if (!reader_.valid()) return 0;
    return reader_.pop_batch(out, max_n);
}

} // namespace td
