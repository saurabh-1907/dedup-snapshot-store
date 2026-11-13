#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dss {

struct ChunkerConfig {
    std::size_t min_size = 2 * 1024;
    std::size_t avg_size = 8 * 1024;   // must be a power of two
    std::size_t max_size = 64 * 1024;
};

struct ChunkSpan {
    std::size_t offset;
    std::size_t length;
};

// Content-defined chunking with a gear rolling hash. Boundaries depend on the
// bytes themselves, so inserting data near the start of a file only changes the
// chunks around the edit instead of shifting every chunk after it.
class Chunker {
public:
    explicit Chunker(ChunkerConfig cfg = {});
    std::vector<ChunkSpan> split(const std::uint8_t* data, std::size_t len) const;

private:
    ChunkerConfig cfg_;
    std::uint64_t mask_;
};

}  // namespace dss
