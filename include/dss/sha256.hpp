#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dss {

// Minimal SHA-256 (FIPS 180-4) so the project has no external crypto dependency.
class Sha256 {
public:
    Sha256();
    void update(const std::uint8_t* data, std::size_t len);
    std::array<std::uint8_t, 32> finish();

    static std::string hex(const std::uint8_t* data, std::size_t len);

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[8];
    std::uint8_t buf_[64];
    std::size_t buf_len_ = 0;
    std::uint64_t total_ = 0;
};

}  // namespace dss
