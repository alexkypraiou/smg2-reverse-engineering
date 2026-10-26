#include "smg2/data/fst.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using smg2::data::FstArchive;
using smg2::data::ParseError;
namespace fs = std::filesystem;

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

std::size_t addString(std::vector<std::uint8_t>& strings, const std::string& value) {
    const std::size_t offset = strings.size();
    strings.insert(strings.end(), value.begin(), value.end());
    strings.push_back(0U);
    return offset;
}

struct Record {
    std::uint32_t typeAndName;
    std::uint32_t parentOrOffset;
    std::uint32_t endOrSize;
};

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> strings;
    const std::size_t rootName = addString(strings, "");
    const std::size_t folder = addString(strings, "folder");
    const std::size_t aName = addString(strings, "a.bin");
    const std::size_t subFolder = addString(strings, "sub");
    const std::size_t bName = addString(strings, "b.bin");
    const auto dir = [](std::size_t nameOffset) {
        return 0x01000000U | static_cast<std::uint32_t>(nameOffset);
    };
    const std::array<Record, 5U> records{{
        {dir(rootName), 0U, 5U},
        {dir(folder), 0U, 5U},
        {static_cast<std::uint32_t>(aName), 0x4000U, 4U},
        {dir(subFolder), 1U, 5U},
        {static_cast<std::uint32_t>(bName), 0x5000U, 8U},
    }};
    std::vector<std::uint8_t> bytes(records.size() * 12U + strings.size(), 0U);
    for (std::size_t i = 0U; i < records.size(); ++i) {
        be32(bytes, i * 12U, records[i].typeAndName);
        be32(bytes, i * 12U + 4U, records[i].parentOrOffset);
        be32(bytes, i * 12U + 8U, records[i].endOrSize);
    }
    std::copy(strings.begin(), strings.end(), bytes.begin() + static_cast<std::ptrdiff_t>(records.size() * 12U));
    return bytes;
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open FST: " + path.string());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void testSynthetic() {
    auto bytes = fixture();
    const FstArchive archive = FstArchive::parse(bytes);
    check(archive.entries().size() == 5U, "FST preserves original node count");
    check(archive.rootName().empty(), "FST root name is empty string");
    const auto* folder = archive.find("folder");
    check(folder && folder->index == 1U && folder->parentIndex == 0U &&
          folder->subtreeEnd == 5U && folder->kind == FstArchive::EntryKind::directory,
          "FST directory index, parent, and subtree range");
    const auto* first = archive.find("folder/a.bin");
    const auto* second = archive.find("folder/sub/b.bin");
    check(first && first->index == 2U && first->parentIndex == 1U &&
          first->rawWordOffset == 0x4000U && first->fileOffset == 0x10000U && first->fileSize == 4U,
          "FST file index and Wii word-to-byte scaling");
    check(second && second->index == 4U && second->parentIndex == 3U &&
          second->fileOffset == 0x14000U && second->fileSize == 8U,
          "FST nested file parent and range");
    check(archive.findFileContainingDiscOffset(0x10000U) == first, "FST disk range contains first byte");
    check(archive.findFileContainingDiscOffset(0x10003U) == first, "FST disk range contains last byte");
    check(archive.findFileContainingDiscOffset(0x10004U) == nullptr, "FST disk range excludes end byte");
    check(archive.findFileContainingDiscOffset(0x14007U) == second, "FST disk range contains nested file");
    check(archive.find("folder/../folder/a.bin") == nullptr, "FST lookup does not normalize traversal");

    auto bad = fixture();
    bad.resize(11U);
    expectParseError([&] { (void)FstArchive::parse(bad); }, "FST rejects truncated root entry");
    bad = fixture(); be32(bad, 8U, 0xFFFFFFFFU);
    expectParseError([&] { (void)FstArchive::parse(bad); }, "FST rejects impossible entry count");
    bad = fixture(); be32(bad, 12U + 4U, 1U);
    expectParseError([&] { (void)FstArchive::parse(bad); }, "FST rejects invalid parent index");
    bad = fixture(); be32(bad, 12U + 8U, 1U);
    expectParseError([&] { (void)FstArchive::parse(bad); }, "FST rejects invalid directory subtree");
    bad = fixture(); be32(bad, 24U, 0x00FFFFFFU);
    expectParseError([&] { (void)FstArchive::parse(bad); }, "FST rejects invalid string offset");
    bad = fixture(); bad.back() = 'X';
    expectParseError([&] { (void)FstArchive::parse(bad); }, "FST rejects unterminated name");
    bad = fixture(); be32(bad, 0U, 0x02000000U);
    expectParseError([&] { (void)FstArchive::parse(bad); }, "FST rejects unsupported root type");
    bad = fixture();
    auto limits = FstArchive::Limits{}; limits.maxEntries = 4U;
    expectParseError([&] { (void)FstArchive::parse(bad, limits); }, "FST entry-count limit");
    limits = FstArchive::Limits{}; limits.maxDepth = 1U;
    expectParseError([&] { (void)FstArchive::parse(bad, limits); }, "FST depth limit");
}

void testRealFst(const fs::path& fstPath, const fs::path& filesRoot) {
    const FstArchive archive = FstArchive::parse(readFile(fstPath));
    std::size_t files = 0U;
    std::size_t missing = 0U;
    std::size_t sizeMismatch = 0U;
    for (const auto& entry : archive.entries()) {
        if (entry.kind != FstArchive::EntryKind::file) continue;
        ++files;
        const fs::path path = filesRoot / fs::path(entry.path);
        std::error_code error;
        if (!fs::is_regular_file(path, error)) {
            ++missing;
            if (missing <= 8U) std::cerr << "FST path absent from extracted DATA/files: " << entry.path << '\n';
            continue;
        }
        const auto size = fs::file_size(path, error);
        if (error || size != entry.fileSize) {
            ++sizeMismatch;
            if (sizeMismatch <= 8U) {
                std::cerr << "FST size mismatch: " << entry.path << " table=" << entry.fileSize
                          << " extracted=" << (error ? 0U : size) << '\n';
            }
        }
    }

    std::size_t extra = 0U;
    for (const auto& item : fs::recursive_directory_iterator(filesRoot)) {
        if (!item.is_regular_file()) continue;
        const std::string rel = fs::relative(item.path(), filesRoot).generic_string();
        const auto* entry = archive.find(rel);
        if (!entry || entry->kind != FstArchive::EntryKind::file) ++extra;
    }
    std::cout << "Real FST: entries=" << archive.entries().size() << " files=" << files
              << " missing=" << missing << " size_mismatches=" << sizeMismatch
              << " extracted_extras=" << extra << '\n';
    check(missing == 0U, "all FST file paths exist in extracted DATA/files");
    check(sizeMismatch == 0U, "all extracted file sizes match FST byte sizes");
    check(extra == 0U, "extracted DATA/files has no files absent from FST");
    check(archive.find("StageData/YosshiHomeGalaxy/YosshiHomeGalaxyMap.arc") != nullptr,
          "real FST lookup resolves a stage map");
}

} // namespace

int main(int argc, char** argv) {
    try {
        testSynthetic();
        if (argc == 3) testRealFst(argv[1], argv[2]);
        else if (argc != 1) throw std::runtime_error("Usage: fst_tests [fst.bin DATA/files]");
        std::cout << "PASS: Wii FST parser (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
