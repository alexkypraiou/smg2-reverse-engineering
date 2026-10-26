#include "smg2/data/u8.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using smg2::data::ParseError;
using smg2::data::U8Archive;

unsigned int checks = 0;

void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error("FAIL: " + message);
}

template <typename Fn>
void expectParseError(Fn&& fn, const std::string& message) {
    bool threw = false;
    try { fn(); } catch (const ParseError&) { threw = true; }
    check(threw, message);
}

void be32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value >> 24U);
    bytes.at(at + 1U) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(at + 2U) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(at + 3U) = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> fixture() {
    constexpr std::size_t root = 0x20U;
    constexpr std::size_t count = 3U;
    constexpr std::size_t strings = root + count * 0x0CU;
    constexpr std::size_t headerSize = count * 0x0CU + 10U;
    constexpr std::size_t dataOffset = 0x50U;
    std::vector<std::uint8_t> bytes(dataOffset + 4U, 0U);
    be32(bytes, 0U, 0x55AA382DU);
    be32(bytes, 4U, static_cast<std::uint32_t>(root));
    be32(bytes, 8U, static_cast<std::uint32_t>(headerSize));
    be32(bytes, 0x0CU, static_cast<std::uint32_t>(dataOffset));
    be32(bytes, root, 0x01000000U); // Root directory, empty name at string offset zero.
    be32(bytes, root + 4U, 0U);
    be32(bytes, root + 8U, static_cast<std::uint32_t>(count));
    be32(bytes, root + 0x0CU, 0x01000001U); // Dir
    be32(bytes, root + 0x10U, 0U);
    be32(bytes, root + 0x14U, 3U);
    be32(bytes, root + 0x18U, 0x00000005U); // file
    be32(bytes, root + 0x1CU, static_cast<std::uint32_t>(dataOffset));
    be32(bytes, root + 0x20U, 4U);
    const std::array<std::uint8_t, 10U> names{{0U, 'D','i','r',0U,'f','i','l','e',0U}};
    std::copy(names.begin(), names.end(), bytes.begin() + static_cast<std::ptrdiff_t>(strings));
    bytes[dataOffset] = 'D'; bytes[dataOffset + 1U] = 'A';
    bytes[dataOffset + 2U] = 'T'; bytes[dataOffset + 3U] = 'A';
    return bytes;
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open archive " + path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void testSynthetic() {
    auto bytes = fixture();
    auto archive = U8Archive::parse(bytes);
    check(archive.entries().size() == 3U, "U8 synthetic entry count");
    check(archive.find("Dir/file") != nullptr, "U8 nested path lookup");
    check(archive.find("/Dir/file") != nullptr, "U8 leading slash lookup");
    check(archive.find("missing") == nullptr, "U8 missing path lookup");
    const auto payload = archive.readFile("Dir/file");
    check(payload.size() == 4U && payload[0] == 'D' && payload[3] == 'A', "U8 payload bytes");
    expectParseError([&] { (void)archive.readFile("Dir"); }, "U8 refuses directory read");
    bytes = fixture(); bytes[0] = 0U;
    expectParseError([&] { (void)U8Archive::parse(bytes); }, "U8 rejects bad signature");
    bytes = fixture(); be32(bytes, 4U, 0xFFFFFFF0U);
    expectParseError([&] { (void)U8Archive::parse(bytes); }, "U8 rejects out-of-range root");
    bytes = fixture(); be32(bytes, 0x20U + 0x18U, 0xFFFFFFFFU);
    expectParseError([&] { (void)U8Archive::parse(bytes); }, "U8 rejects file payload out of bounds");
    bytes = fixture(); be32(bytes, 0x20U + 0x14U, 1U);
    expectParseError([&] { (void)U8Archive::parse(bytes); }, "U8 rejects invalid directory subtree");
    bytes = fixture(); be32(bytes, 8U, 0x40U);
    expectParseError([&] { (void)U8Archive::parse(bytes); }, "U8 rejects truncated node table");
}

void testRealArchive(const std::string& path) {
    const auto bytes = readFile(path);
    const auto archive = U8Archive::parse(bytes);
    check(archive.entries().size() > 1U, "real U8 has multiple entries: " + path);
    check(!archive.entries().empty(), "real U8 has root: " + path);
    std::size_t files = 0U;
    for (const auto& entry : archive.entries()) {
        if (entry.kind == U8Archive::EntryKind::file) {
            const auto data = archive.readFile(entry);
            check(data.size() == entry.storedSize, "real U8 file bounds: " + path);
            ++files;
        }
    }
    check(files != 0U, "real U8 contains files: " + path);
    std::cout << "U8 OK: " << path << " entries=" << archive.entries().size()
              << " files=" << files << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        testSynthetic();
        for (int i = 1; i < argc; ++i) testRealArchive(argv[i]);
        std::cout << "PASS: native U8 archive reader (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
