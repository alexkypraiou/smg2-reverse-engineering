#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace smg2::data {

// JGadget uses signed char input on the Wii; arithmetic wraps to 32 bits.
[[nodiscard]] std::uint32_t jmap_hash(std::string_view name) noexcept;

enum class BcsvType : std::uint8_t {
    long_value = 0, inline_string = 1, float_value = 2,
    long_value_2 = 3, short_value = 4, byte_value = 5, string_offset = 6
};

struct BcsvField {
    std::uint32_t hash{};
    std::uint32_t mask{};
    std::uint16_t offset{};
    std::uint8_t shift{};
    BcsvType type{};
};

struct BcsvLimits {
    std::size_t max_bytes{256U * 1024U * 1024U};
    std::uint32_t max_entries{1000000U};
    std::uint32_t max_fields{4096U};
};

// Owns its bytes. String views remain valid while this object is unmodified/alive.
// Layout/operations are source-informed by JMapInfo and verified PPC assembly.
class BcsvTable {
public:
    [[nodiscard]] static BcsvTable parse(std::span<const std::uint8_t> bytes,
                                         BcsvLimits limits = {});
    [[nodiscard]] std::uint32_t row_count() const noexcept { return rows_; }
    [[nodiscard]] std::uint32_t row_size() const noexcept { return row_size_; }
    [[nodiscard]] std::span<const BcsvField> fields() const noexcept { return fields_; }
    [[nodiscard]] std::optional<std::size_t> find_field(std::string_view name) const noexcept;

    // Integer and string accessors follow JMapInfo's type dispatch. The native
    // float reader is intentionally type-2-only; DOL's f32 overload reads the
    // same four bytes without checking the descriptor type.
    // Invalid row/field indices throw; malformed strings throw at read time.
    [[nodiscard]] std::optional<std::uint32_t> unsigned_value(std::size_t row, std::size_t field) const;
    [[nodiscard]] std::optional<std::int32_t> signed_value(std::size_t row, std::size_t field) const;
    [[nodiscard]] std::optional<float> float_value(std::size_t row, std::size_t field) const;
    [[nodiscard]] std::optional<std::string_view> string_value(std::size_t row, std::size_t field) const;

private:
    [[nodiscard]] std::size_t value_offset(std::size_t row, std::size_t field) const;
    std::vector<std::uint8_t> bytes_;
    std::vector<BcsvField> fields_;
    std::uint32_t rows_{};
    std::uint32_t row_size_{};
    std::size_t data_offset_{};
    std::size_t strings_offset_{};
};

} // namespace smg2::data
