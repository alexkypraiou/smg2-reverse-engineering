#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace smg2::data {

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Yaz0 {
public:
    static constexpr std::size_t kHeaderSize = 0x10;
    static constexpr std::size_t kDefaultMaxOutputBytes = 512U * 1024U * 1024U;

    [[nodiscard]] static bool hasSignature(std::span<const std::uint8_t> source) noexcept;
    [[nodiscard]] static std::size_t expandedSize(std::span<const std::uint8_t> source);
    [[nodiscard]] static std::vector<std::uint8_t> decompress(
        std::span<const std::uint8_t> source,
        std::size_t maxOutputBytes = kDefaultMaxOutputBytes);
};

} // namespace smg2::data
