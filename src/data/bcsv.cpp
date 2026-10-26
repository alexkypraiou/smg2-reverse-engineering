#include "smg2/data/bcsv.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace smg2::data {
namespace {
std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[at]) << 8U) | b[at + 1]);
}
std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24U) |
           (static_cast<std::uint32_t>(b[at + 1]) << 16U) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8U) | b[at + 3];
}
std::size_t width(BcsvType type) {
    switch (type) {
    case BcsvType::inline_string: case BcsvType::byte_value: return 1;
    case BcsvType::short_value: return 2;
    case BcsvType::long_value: case BcsvType::long_value_2:
    case BcsvType::float_value: case BcsvType::string_offset: return 4;
    }
    throw std::runtime_error("BCSV field has unsupported type");
}
} // namespace

std::uint32_t jmap_hash(std::string_view name) noexcept {
    std::uint32_t result = 0;
    for (char c : name) {
        const auto byte = static_cast<std::uint8_t>(c);
        if (byte == 0) break;
        const std::int32_t signed_byte = byte < 128U ? byte : static_cast<std::int32_t>(byte) - 256;
        result = result * 31U + static_cast<std::uint32_t>(signed_byte);
    }
    return result;
}

BcsvTable BcsvTable::parse(std::span<const std::uint8_t> bytes, BcsvLimits limits) {
    if (bytes.size() < 16 || bytes.size() > limits.max_bytes) {
        throw std::runtime_error("BCSV header truncated or input exceeds limit");
    }
    BcsvTable result;
    result.rows_ = be32(bytes, 0);
    const std::uint32_t count = be32(bytes, 4);
    result.data_offset_ = be32(bytes, 8);
    result.row_size_ = be32(bytes, 12);
    if (result.rows_ > limits.max_entries || result.rows_ > 0x7fffffffU ||
        count > limits.max_fields || count > 0x7fffffffU ||
        (result.rows_ != 0 && result.row_size_ == 0)) {
        throw std::runtime_error("BCSV dimensions invalid or exceed limits");
    }
    if (count > (bytes.size() - 16) / 12 ||
        result.data_offset_ < 16U + static_cast<std::size_t>(count) * 12U ||
        result.data_offset_ > bytes.size()) {
        throw std::runtime_error("BCSV field/data table outside input");
    }
    if (result.row_size_ != 0 &&
        result.rows_ > (bytes.size() - result.data_offset_) / result.row_size_) {
        throw std::runtime_error("BCSV record table truncated");
    }
    result.strings_offset_ = result.data_offset_ +
        static_cast<std::size_t>(result.rows_) * result.row_size_;
    result.fields_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 16 + i * 12;
        BcsvField field{be32(bytes, at), be32(bytes, at + 4), be16(bytes, at + 8),
                        bytes[at + 10], static_cast<BcsvType>(bytes[at + 11])};
        const std::size_t size = width(field.type);
        if (field.offset > result.row_size_ || size > result.row_size_ - field.offset ||
            field.shift >= 32) {
            throw std::runtime_error("BCSV field does not fit its record or has invalid shift");
        }
        result.fields_.push_back(field);
    }
    result.bytes_.assign(bytes.begin(), bytes.end());
    return result;
}

std::optional<std::size_t> BcsvTable::find_field(std::string_view name) const noexcept {
    const auto hash = jmap_hash(name);
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i].hash == hash) return i;
    }
    return std::nullopt;
}

std::size_t BcsvTable::value_offset(std::size_t row, std::size_t field) const {
    if (row >= rows_ || field >= fields_.size()) throw std::out_of_range("BCSV row/field index");
    return data_offset_ + row * row_size_ + fields_[field].offset;
}

std::optional<std::uint32_t> BcsvTable::unsigned_value(std::size_t row, std::size_t field) const {
    const auto at = value_offset(row, field);
    const auto& f = fields_[field];
    std::uint32_t raw = 0;
    switch (f.type) {
    case BcsvType::long_value: case BcsvType::long_value_2: raw = be32(bytes_, at); break;
    case BcsvType::short_value: raw = be16(bytes_, at); break;
    case BcsvType::byte_value: raw = bytes_[at]; break;
    default: return std::nullopt;
    }
    return (raw & f.mask) >> f.shift;
}

std::optional<std::int32_t> BcsvTable::signed_value(std::size_t row, std::size_t field) const {
    const auto at = value_offset(row, field);
    const auto& f = fields_[field];
    if (f.shift != 0) return std::nullopt;
    switch (f.type) {
    case BcsvType::long_value:
        if (f.mask == 0xffffffffU) return std::bit_cast<std::int32_t>(be32(bytes_, at));
        break;
    case BcsvType::short_value:
        if (f.mask == 0xffffU) return std::bit_cast<std::int16_t>(be16(bytes_, at));
        break;
    case BcsvType::byte_value:
        if (f.mask == 0xffU) return std::bit_cast<std::int8_t>(bytes_[at]);
        break;
    default: break;
    }
    return std::nullopt;
}

std::optional<float> BcsvTable::float_value(std::size_t row, std::size_t field) const {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    const auto at = value_offset(row, field);
    if (fields_[field].type != BcsvType::float_value) return std::nullopt;
    return std::bit_cast<float>(be32(bytes_, at));
}

std::optional<std::string_view> BcsvTable::string_value(std::size_t row, std::size_t field) const {
    std::size_t at = value_offset(row, field);
    const auto& f = fields_[field];
    std::size_t limit = bytes_.size();
    if (f.type == BcsvType::string_offset) {
        const std::size_t relative = be32(bytes_, at);
        if (relative >= bytes_.size() - strings_offset_) {
            throw std::runtime_error("BCSV string offset outside string table");
        }
        at = strings_offset_ + relative;
    } else if (f.type == BcsvType::inline_string) {
        std::size_t end_offset = row_size_;
        for (const auto& next : fields_) {
            if (next.offset > f.offset) end_offset = std::min(end_offset, static_cast<std::size_t>(next.offset));
        }
        limit = data_offset_ + row * row_size_ + end_offset;
    } else {
        return std::nullopt;
    }
    const auto* first = bytes_.data() + at;
    const auto* end = std::find(first, bytes_.data() + limit, static_cast<std::uint8_t>(0));
    if (end == bytes_.data() + limit) throw std::runtime_error("BCSV string is not terminated within bounds");
    return std::string_view(reinterpret_cast<const char*>(first), static_cast<std::size_t>(end - first));
}

} // namespace smg2::data
