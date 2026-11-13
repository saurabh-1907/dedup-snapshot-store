#include "dss/chunker.hpp"

#include <array>
#include <stdexcept>

namespace dss {
namespace {
// Deterministic 256-entry gear table generated with splitmix64.
const std::array<std::uint64_t, 256>& gear_table() {
    static const std::array<std::uint64_t, 256> table = [] {
        std::array<std::uint64_t, 256> t{};
        std::uint64_t x = 0x9E3779B97F4A7C15ull;
        for (auto& v : t) {
            x += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            v = z ^ (z >> 31);
        }
        return t;
    }();
    return table;
}
}  // namespace

Chunker::Chunker(ChunkerConfig cfg) : cfg_(cfg) {
    if (cfg_.avg_size == 0 || (cfg_.avg_size & (cfg_.avg_size - 1)) != 0)
        throw std::invalid_argument("avg_size must be a power of two");
    if (!(cfg_.min_size <= cfg_.avg_size && cfg_.avg_size <= cfg_.max_size))
        throw std::invalid_argument("require min_size <= avg_size <= max_size");
    mask_ = cfg_.avg_size - 1;
}

std::vector<ChunkSpan> Chunker::split(const std::uint8_t* data, std::size_t len) const {
    const auto& gear = gear_table();
    std::vector<ChunkSpan> out;
    std::size_t start = 0;
    while (start < len) {
        std::size_t remaining = len - start;
        if (remaining <= cfg_.min_size) {
            out.push_back({start, remaining});
            break;
        }
        std::size_t limit = std::min(remaining, cfg_.max_size);
        std::uint64_t hash = 0;
        std::size_t cut = limit;
        for (std::size_t i = cfg_.min_size; i < limit; ++i) {
            hash = (hash << 1) + gear[data[start + i]];
            if ((hash & mask_) == 0) { cut = i + 1; break; }
        }
        out.push_back({start, cut});
        start += cut;
    }
    return out;
}

}  // namespace dss
