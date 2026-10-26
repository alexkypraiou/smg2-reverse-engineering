#include "smg2/data/yaz0.hpp"

#include <algorithm>
#include <limits>

namespace smg2::data {
namespace {

std::uint32_t readBigEndian32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

} // namespace

bool Yaz0::hasSignature(std::span<const std::uint8_t> source) noexcept {
    return source.size() >= 4U && source[0] == 'Y' && source[1] == 'a' &&
           source[2] == 'z' && source[3] == '0';
}

std::size_t Yaz0::expandedSize(std::span<const std::uint8_t> source) {
    if (!hasSignature(source)) throw ParseError("Yaz0 signature is missing");
    if (source.size() < kHeaderSize) throw ParseError("Yaz0 header is truncated");
    return static_cast<std::size_t>(readBigEndian32(source, 4U));
}

std::vector<std::uint8_t> Yaz0::decompress(std::span<const std::uint8_t> source,
                                           std::size_t maxOutputBytes) {
    const std::size_t outputSize = expandedSize(source);
    if (outputSize > maxOutputBytes) throw ParseError("Yaz0 output exceeds configured size limit");
    std::vector<std::uint8_t> output;
    output.reserve(outputSize);

    std::size_t input = kHeaderSize;
    while (output.size() < outputSize) {
        if (input >= source.size()) throw ParseError("Yaz0 control byte is truncated");
        const std::uint8_t control = source[input++];

        for (unsigned bit = 0U; bit < 8U && output.size() < outputSize; ++bit) {
            if ((control & static_cast<std::uint8_t>(0x80U >> bit)) != 0U) {
                if (input >= source.size()) throw ParseError("Yaz0 literal is truncated");
                output.push_back(source[input++]);
                continue;
            }

            if (source.size() - input < 2U) throw ParseError("Yaz0 back-reference is truncated");
            const std::uint8_t first = source[input++];
            const std::uint8_t second = source[input++];
            const std::size_t distance =
                ((static_cast<std::size_t>(first & 0x0FU) << 8U) | second) + 1U;
            std::size_t length = static_cast<std::size_t>(first >> 4U);
            if (length == 0U) {
                if (input >= source.size()) throw ParseError("Yaz0 extended back-reference is truncated");
                length = static_cast<std::size_t>(source[input++]) + 0x12U;
            } else {
                length += 2U;
            }

            if (distance > output.size()) throw ParseError("Yaz0 back-reference points before output start");
            const std::size_t count = std::min(length, outputSize - output.size());
            for (std::size_t i = 0U; i < count; ++i) {
                output.push_back(output[output.size() - distance]);
            }
        }
    }

    return output;
}

} // namespace smg2::data
