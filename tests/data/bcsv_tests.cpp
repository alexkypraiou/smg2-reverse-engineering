#include "smg2/data/bcsv.hpp"

#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

using namespace smg2::data;

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, std::string_view label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

template <class F> void throws(F&& f, std::string_view label) {
    ++checks;
    try {
        f();
        ++failures;
        std::cerr << "FAIL: expected exception: " << label << '\n';
    } catch (const std::exception&) {
    }
}

void be16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b.at(at) = static_cast<std::uint8_t>(v >> 8U);
    b.at(at + 1) = static_cast<std::uint8_t>(v);
}
void be32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b.at(at) = static_cast<std::uint8_t>(v >> 24U);
    b.at(at + 1) = static_cast<std::uint8_t>(v >> 16U);
    b.at(at + 2) = static_cast<std::uint8_t>(v >> 8U);
    b.at(at + 3) = static_cast<std::uint8_t>(v);
}

struct Spec { std::uint32_t hash, mask; std::uint16_t offset; std::uint8_t shift, type; };

std::vector<std::uint8_t> fixture(std::uint32_t rows = 2) {
    // Seven consecutive JMapInfo field descriptors, then two 36-byte records,
    // then the string table (which starts after every record, not row zero).
    constexpr std::uint32_t fields = 7, rowSize = 36;
    constexpr std::size_t data = 16 + fields * 12;
    const std::size_t strings = data + rows * rowSize;
    const Spec specs[] = {
        {jmap_hash("masked"), 0x00ffff00U, 0, 8, 0},
        {jmap_hash("inline"), 0xffffffffU, 4, 0, 1},
        {jmap_hash("ratio"), 0xffffffffU, 20, 0, 2},
        {jmap_hash("packed"), 0x00ff0000U, 24, 16, 3},
        {jmap_hash("signed16"), 0xffffU, 28, 0, 4},
        {jmap_hash("signed8"), 0xffU, 30, 0, 5},
        {jmap_hash("table"), 0xffffffffU, 32, 0, 6},
    };
    std::vector<std::uint8_t> b(strings + 12, 0);
    be32(b, 0, rows); be32(b, 4, fields); be32(b, 8, data); be32(b, 12, rowSize);
    for (std::size_t i = 0; i < fields; ++i) {
        const auto at = 16 + i * 12;
        be32(b, at, specs[i].hash); be32(b, at + 4, specs[i].mask);
        be16(b, at + 8, specs[i].offset);
        b[at + 10] = specs[i].shift; b[at + 11] = specs[i].type;
    }
    for (std::uint32_t row = 0; row < rows; ++row) {
        const auto base = data + row * rowSize;
        be32(b, base, row == 0 ? 0x12abcdefU : 0x00012300U);
        b[base + 4] = 'c'; b[base + 5] = 'a'; b[base + 6] = 't'; b[base + 7] = 0;
        be32(b, base + 20, std::bit_cast<std::uint32_t>(row == 0 ? 1.25F : -2.5F));
        be32(b, base + 24, 0x10203040U);
        be16(b, base + 28, 0xff80U); b[base + 30] = 0x80U;
        be32(b, base + 32, 0);
    }
    const char table[] = "star\0";
    for (std::size_t i = 0; i < sizeof(table); ++i) b[strings + i] = static_cast<std::uint8_t>(table[i]);
    return b;
}

void values_and_layout() {
    auto bytes = fixture();
    auto table = BcsvTable::parse(bytes);
    check(table.row_count() == 2 && table.row_size() == 36, "header dimensions");
    check(table.fields().size() == 7, "field descriptor count");
    check(table.find_field("masked") == 0, "field lookup uses JGadget hash");
    check(!table.find_field("missing"), "unknown field absent");
    check(table.unsigned_value(0, 0) == 0xabcdu, "type 0 applies mask then shift");
    check(table.unsigned_value(1, 0) == 0x123u, "type 0 field read from second row");
    check(table.string_value(0, 1) == "cat", "type 1 inline string");
    check(table.float_value(0, 2) == 1.25F && table.float_value(1, 2) == -2.5F,
          "type 2 endian float bits");
    check(table.unsigned_value(0, 3) == 0x20u, "type 3 long value and mask/shift");
    check(!table.signed_value(0, 3), "type 3 is deliberately rejected by signed accessor");
    check(table.unsigned_value(0, 4) == 0xff80U && table.signed_value(0, 4) == -128,
          "type 4 unsigned and full-width signed reads");
    check(table.unsigned_value(0, 5) == 0x80U && table.signed_value(0, 5) == -128,
          "type 5 unsigned and full-width signed reads");
    check(table.string_value(1, 6) == "star", "type 6 relative string table offset");
    check(!table.unsigned_value(0, 1) && !table.float_value(0, 1) && !table.string_value(0, 0),
          "typed accessor rejects incompatible field kinds");
    throws([&] { (void)table.unsigned_value(2, 0); }, "row index checked");
    throws([&] { (void)table.float_value(0, 7); }, "field index checked");
}

void hash_semantics() {
    check(jmap_hash("Mario") == 0x046ae2e4U, "ASCII hash uses multiply by 31");
    const char high[] = {static_cast<char>(0xc3), static_cast<char>(0xa9), 0};
    check(jmap_hash(std::string_view(high, 2)) == 0xfffff846U,
          "hash sign-extends high bytes as PPC extsb does");
    check(jmap_hash(std::string_view("ab\0tail", 7)) == jmap_hash("ab"),
          "hash stops at nul like C string routine");
}

void malformed_inputs() {
    auto good = fixture();
    throws([&] { (void)BcsvTable::parse(std::span<const std::uint8_t>(good).first(15)); }, "short header");
    throws([&] { (void)BcsvTable::parse(good, BcsvLimits{32, 100, 100}); }, "byte cap");
    throws([&] { (void)BcsvTable::parse(good, BcsvLimits{good.size(), 1, 100}); }, "entry cap");
    throws([&] { (void)BcsvTable::parse(good, BcsvLimits{good.size(), 100, 6}); }, "field cap");

    auto b = good; be32(b, 8, 16); throws([&] { (void)BcsvTable::parse(b); }, "data overlaps descriptor table");
    b = good; be32(b, 8, static_cast<std::uint32_t>(b.size() + 1)); throws([&] { (void)BcsvTable::parse(b); }, "data offset beyond file");
    b = good; be32(b, 0, 100); throws([&] { (void)BcsvTable::parse(b); }, "row dimensions exceed available data");
    b = good; be32(b, 4, 0xffffffffU); throws([&] { (void)BcsvTable::parse(b); }, "field descriptor multiplication is bounded");
    b = good; b[16 + 11] = 7; throws([&] { (void)BcsvTable::parse(b); }, "unknown field type");
    b = good; b[16 + 10] = 32; throws([&] { (void)BcsvTable::parse(b); }, "shift 32 rejected");
    b = good; be16(b, 16 + 8, 35); throws([&] { (void)BcsvTable::parse(b); }, "field width crosses record");
    b = good; be16(b, 16 + 12 + 8, 36); throws([&] { (void)BcsvTable::parse(b); }, "inline field offset crosses record");
    b = good; be32(b, 12, 0); throws([&] { (void)BcsvTable::parse(b); }, "nonempty zero-sized rows rejected");

    // A valid empty table may have no records; this avoids conflating a valid
    // empty dimensions case with corrupt nonempty records.
    std::vector<std::uint8_t> empty(16, 0);
    be32(empty, 8, 16);
    check(BcsvTable::parse(empty).row_count() == 0, "empty table accepted");
}

void string_bounds() {
    auto b = fixture();
    auto table = BcsvTable::parse(b);
    auto strings = b.size() - 12;
    constexpr std::size_t rowData = 16 + 7 * 12;
    be32(b, rowData + 32, 12); // relative offset exactly at EOF
    table = BcsvTable::parse(b);
    throws([&] { (void)table.string_value(0, 6); }, "type 6 offset at end rejected");
    be32(b, rowData + 32, 5);
    std::fill(b.begin() + static_cast<std::ptrdiff_t>(strings), b.end(), static_cast<std::uint8_t>('x'));
    table = BcsvTable::parse(b);
    throws([&] { (void)table.string_value(0, 6); }, "unterminated type 6 string rejected");

    b = fixture();
    // Inline string is bounded by next field offset 20. Remove its terminator.
    const std::size_t data = 16 + 7 * 12;
    std::fill(b.begin() + static_cast<std::ptrdiff_t>(data + 4),
              b.begin() + static_cast<std::ptrdiff_t>(data + 20), static_cast<std::uint8_t>('x'));
    table = BcsvTable::parse(b);
    throws([&] { (void)table.string_value(0, 1); }, "inline string cannot run into next field");

    b = fixture(1);
    const auto oneData = 16 + 7 * 12;
    // The next field at offset 20 bounds the string; fill its full permitted
    // span to verify the parser never scans into that field.
    std::fill(b.begin() + static_cast<std::ptrdiff_t>(oneData + 4),
              b.begin() + static_cast<std::ptrdiff_t>(oneData + 20), static_cast<std::uint8_t>('q'));
    table = BcsvTable::parse(b);
    throws([&] { (void)table.string_value(0, 1); }, "inline unterminated string rejected at field boundary");
}
} // namespace

int main() {
    values_and_layout();
    hash_semantics();
    malformed_inputs();
    string_bounds();
    std::cout << checks - failures << '/' << checks << " BCSV checks passed\n";
    return failures == 0 ? 0 : 1;
}
