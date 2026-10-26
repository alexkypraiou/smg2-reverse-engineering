#include "smg2/data/rarc.hpp"
#include "smg2/data/yaz0.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using smg2::data::ParseError;
using smg2::data::RarcArchive;
using smg2::data::Yaz0;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, std::string_view label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

template <class Callback>
void expectParseError(Callback&& callback, std::string_view label) {
    ++checks;
    try {
        callback();
        ++failures;
        std::cerr << "FAIL: expected parse error: " << label << '\n';
    } catch (const ParseError&) {
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL: wrong exception for " << label << ": " << error.what() << '\n';
    }
}

void writeBe16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(at + 1U) = static_cast<std::uint8_t>(value);
}

void writeBe32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value >> 24U);
    bytes.at(at + 1U) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(at + 2U) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(at + 3U) = static_cast<std::uint8_t>(value);
}

std::size_t appendString(std::vector<std::uint8_t>& strings, std::string_view value) {
    const std::size_t offset = strings.size();
    for (const char c : value) strings.push_back(static_cast<std::uint8_t>(c));
    strings.push_back(0U);
    return offset;
}

struct EntrySpec {
    std::uint16_t id;
    std::uint16_t flags;
    std::uint16_t nameOffset;
    std::uint32_t dataOffset;
    std::uint32_t size;
};

std::vector<std::uint8_t> makeRarc() {
    constexpr std::size_t header = 0x20U;
    constexpr std::size_t infoSize = 0x20U;
    constexpr std::size_t nodeOffset = 0x20U;
    constexpr std::size_t fileOffset = 0x40U;
    constexpr std::size_t fileCount = 6U;
    constexpr std::size_t fileTableSize = fileCount * 0x14U;
    constexpr std::size_t stringOffset = fileOffset + fileTableSize;

    std::vector<std::uint8_t> strings;
    const auto rootName = appendString(strings, "Root");
    const auto dotName = appendString(strings, ".");
    const auto dotDotName = appendString(strings, "..");
    const auto directoryName = appendString(strings, "Maps");
    const auto fileName = appendString(strings, "Stage.bin");
    if (rootName != 0U) throw std::runtime_error("fixture root name offset must be zero");

    const std::size_t fileDataOffset = stringOffset + strings.size();
    const std::size_t dataBase = header + fileDataOffset;
    const std::array<std::uint8_t, 4> payload{{'D', 'A', 'T', 'A'}};
    std::vector<std::uint8_t> bytes(dataBase + payload.size(), 0U);

    bytes[0] = 'R'; bytes[1] = 'A'; bytes[2] = 'R'; bytes[3] = 'C';
    writeBe32(bytes, 4U, static_cast<std::uint32_t>(bytes.size()));
    writeBe32(bytes, 8U, static_cast<std::uint32_t>(header));
    writeBe32(bytes, 0x0CU, static_cast<std::uint32_t>(fileDataOffset));
    writeBe32(bytes, 0x10U, static_cast<std::uint32_t>(payload.size()));
    writeBe32(bytes, 0x14U, static_cast<std::uint32_t>(payload.size()));
    writeBe32(bytes, 0x18U, 0U);

    const std::size_t info = header;
    writeBe32(bytes, info, 2U);
    writeBe32(bytes, info + 4U, static_cast<std::uint32_t>(nodeOffset));
    writeBe32(bytes, info + 8U, static_cast<std::uint32_t>(fileCount));
    writeBe32(bytes, info + 0x0CU, static_cast<std::uint32_t>(fileOffset));
    writeBe32(bytes, info + 0x10U, static_cast<std::uint32_t>(strings.size()));
    writeBe32(bytes, info + 0x14U, static_cast<std::uint32_t>(stringOffset));

    const std::size_t nodes = info + nodeOffset;
    writeBe32(bytes, nodes, 0x524F4F54U); // ROOT
    writeBe32(bytes, nodes + 4U, static_cast<std::uint32_t>(rootName));
    writeBe16(bytes, nodes + 0x0AU, 3U);
    writeBe32(bytes, nodes + 0x0CU, 0U);
    writeBe32(bytes, nodes + 0x10U, 0x4D415053U); // MAPS
    writeBe32(bytes, nodes + 0x14U, static_cast<std::uint32_t>(directoryName));
    writeBe16(bytes, nodes + 0x1AU, 3U);
    writeBe32(bytes, nodes + 0x1CU, 3U);

    const std::array<EntrySpec, fileCount> entries{{
        {0xFFFFU, 0x02U, static_cast<std::uint16_t>(dotName), 0U, 0x10U},
        {0xFFFFU, 0x02U, static_cast<std::uint16_t>(dotDotName), 0U, 0x10U},
        {1U, 0x02U, static_cast<std::uint16_t>(directoryName), 1U, 0x10U},
        {0xFFFFU, 0x02U, static_cast<std::uint16_t>(dotName), 1U, 0x10U},
        {0xFFFFU, 0x02U, static_cast<std::uint16_t>(dotDotName), 0U, 0x10U},
        {2U, 0x95U, static_cast<std::uint16_t>(fileName), 0U, 4U},
    }};
    const std::size_t table = info + fileOffset;
    for (std::size_t i = 0U; i < entries.size(); ++i) {
        const std::size_t at = table + i * 0x14U;
        writeBe16(bytes, at, entries[i].id);
        writeBe16(bytes, at + 2U, 0U);
        bytes[at + 4U] = static_cast<std::uint8_t>(entries[i].flags);
        bytes[at + 5U] = static_cast<std::uint8_t>(entries[i].nameOffset >> 16U);
        bytes[at + 6U] = static_cast<std::uint8_t>(entries[i].nameOffset >> 8U);
        bytes[at + 7U] = static_cast<std::uint8_t>(entries[i].nameOffset);
        writeBe32(bytes, at + 8U, entries[i].dataOffset);
        writeBe32(bytes, at + 0x0CU, entries[i].size);
        writeBe32(bytes, at + 0x10U, 0U);
    }
    std::copy(strings.begin(), strings.end(), bytes.begin() + static_cast<std::ptrdiff_t>(info + stringOffset));
    std::copy(payload.begin(), payload.end(), bytes.begin() + static_cast<std::ptrdiff_t>(dataBase));
    (void)infoSize;
    return bytes;
}

std::vector<std::uint8_t> makeYaz0(std::size_t outputSize,
                                  std::span<const std::uint8_t> body) {
    std::vector<std::uint8_t> bytes(0x10U + body.size(), 0U);
    bytes[0] = 'Y'; bytes[1] = 'a'; bytes[2] = 'z'; bytes[3] = '0';
    writeBe32(bytes, 4U, static_cast<std::uint32_t>(outputSize));
    std::copy(body.begin(), body.end(), bytes.begin() + 0x10);
    return bytes;
}

void testYaz0() {
    const std::array<std::uint8_t, 4> literalBody{{0xE0U, 'A', 'B', 'C'}};
    const auto literal = makeYaz0(3U, literalBody);
    check(Yaz0::hasSignature(literal), "Yaz0 signature recognized");
    check(Yaz0::expandedSize(literal) == 3U, "Yaz0 declared expansion size read big-endian");
    check(Yaz0::decompress(literal) == std::vector<std::uint8_t>({'A', 'B', 'C'}),
          "Yaz0 literal group decodes");

    const std::array<std::uint8_t, 6> overlappingBody{{0xE8U, 'A', 'B', 'C', 0x40U, 0x02U}};
    const auto overlapping = makeYaz0(9U, overlappingBody);
    check(Yaz0::decompress(overlapping) == std::vector<std::uint8_t>({'A','B','C','A','B','C','A','B','C'}),
          "Yaz0 overlapping back-reference expands forward");

    const std::array<std::uint8_t, 5> extendedBody{{0x80U, 'Z', 0x00U, 0x00U, 0x05U}};
    const auto extended = Yaz0::decompress(makeYaz0(18U, extendedBody));
    check(extended.size() == 18U && std::all_of(extended.begin(), extended.end(), [](auto b) { return b == 'Z'; }),
          "Yaz0 extended length code decodes");

    expectParseError([] { (void)Yaz0::expandedSize(std::array<std::uint8_t, 4>{'N','O','P','E'}); },
                     "wrong Yaz0 signature");
    expectParseError([] { (void)Yaz0::expandedSize(std::array<std::uint8_t, 4>{'Y','a','z','0'}); },
                     "truncated Yaz0 header");
    expectParseError([&] { (void)Yaz0::decompress(literal, 2U); }, "Yaz0 configured size cap");
    const std::array<std::uint8_t, 1> truncatedBody{{0x80U}};
    expectParseError([&] { (void)Yaz0::decompress(makeYaz0(1U, truncatedBody)); }, "truncated Yaz0 literal");
    const std::array<std::uint8_t, 3> invalidReference{{0x00U, 0x00U, 0x00U}};
    expectParseError([&] { (void)Yaz0::decompress(makeYaz0(3U, invalidReference)); },
                     "Yaz0 reference before output start");
}

void testRarc() {
    auto encoded = makeRarc();
    auto archive = RarcArchive::parse(encoded);
    check(archive.rootName() == "Root", "RARC root node name is read");
    check(archive.entries().size() == 3U, "RARC root, directory, and file entries are listed");
    const auto* directory = archive.find("Maps");
    check(directory != nullptr && directory->kind == RarcArchive::EntryKind::directory,
          "RARC child directory is indexed");
    const auto* file = archive.find("Maps/Stage.bin");
    check(file != nullptr && file->kind == RarcArchive::EntryKind::file && file->id == 2U,
          "RARC child file is indexed with id");
    check(file != nullptr && file->flags == 0x95U,
          "RARC file preserves Yaz0 and memory-class attribute bits");
    const auto stored = archive.readFile("/Maps/Stage.bin");
    check(stored.size() == 4U && stored[0] == 'D' && stored[1] == 'A' &&
          stored[2] == 'T' && stored[3] == 'A', "RARC file lookup returns stored bytes");
    check(archive.find("Maps/../Maps/Stage.bin") == nullptr, "RARC lookup does not normalize parent traversal");
    expectParseError([&] { (void)archive.readFile("Maps"); }, "RARC directory read rejected");
    expectParseError([&] { (void)archive.readFile("missing.bin"); }, "RARC missing file rejected");

    encoded.back() = 'X';
    check(archive.readFile("Maps/Stage.bin")[3] == 'A', "RARC parser owns an immutable copy of source data");

    auto bad = makeRarc();
    bad[0] = 'X';
    expectParseError([&] { (void)RarcArchive::parse(bad); }, "wrong RARC signature");
    bad = makeRarc();
    writeBe32(bad, 0x2CU, 0xFFFFFFF0U);
    expectParseError([&] { (void)RarcArchive::parse(bad); }, "RARC node-table offset checked");
    bad = makeRarc();
    writeBe32(bad, 4U, 0xFFFFFFFFU);
    expectParseError([&] { (void)RarcArchive::parse(bad); }, "RARC declared size checked");
    bad = makeRarc();
    writeBe32(bad, 0x60U + 2U * 0x14U + 8U, 0xFFFFFFFFU);
    expectParseError([&] { (void)RarcArchive::parse(bad); }, "RARC child-node reference checked");
    bad = makeRarc();
    writeBe32(bad, 0x40U + 0x0CU, 0xFFFFFFFFU);
    expectParseError([&] { (void)RarcArchive::parse(bad); }, "RARC node entry span checked");
    bad = makeRarc();
    auto limits = RarcArchive::Limits{};
    limits.maxEntries = 1U;
    expectParseError([&] { (void)RarcArchive::parse(bad, limits); }, "RARC entry-count cap");
}

void probeAsset(const char* path) {
    ++checks;
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error(std::string("could not open ") + path);
        const std::vector<std::uint8_t> source{
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        std::vector<std::uint8_t> expanded;
        if (Yaz0::hasSignature(source)) expanded = Yaz0::decompress(source);
        else expanded = source;
        const auto archive = RarcArchive::parse(expanded);
        const std::size_t files = static_cast<std::size_t>(std::count_if(
            archive.entries().begin(), archive.entries().end(),
            [](const RarcArchive::Entry& entry) { return entry.kind == RarcArchive::EntryKind::file; }));
        std::cout << "RARC OK: " << path << " root=" << archive.rootName()
                  << " entries=" << archive.entries().size() << " files=" << files << '\n';
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL: asset " << path << ": " << error.what() << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        testYaz0();
        testRarc();
        for (int i = 1; i < argc; ++i) probeAsset(argv[i]);
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << '/' << checks << " checks failed\n";
        return 1;
    }
    std::cout << "PASS: native Yaz0/RARC loaders (" << checks << " checks)\n";
    return 0;
}
