#pragma once

#include "smg2/data/yaz0.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace smg2::data {

// Read-only parser/index for the Wii DVD File System Table (FST).
// The input is the raw sequence of 12-byte FST nodes followed by the name table.
class FstArchive {
public:
    enum class EntryKind : std::uint8_t { directory, file };

    struct Entry {
        std::uint32_t index{};       // Original zero-based FST node index.
        std::uint32_t parentIndex{}; // Inferred from the FST preorder for files.
        std::uint32_t subtreeEnd{};  // Exclusive node index; files use index + 1.
        std::string path;            // Relative to FST root; root path is empty.
        std::string name;            // Raw game bytes; commonly ASCII/Shift-JIS.
        EntryKind kind{};
        std::uint32_t rawWordOffset{}; // File entries only; DVD word offset from the FST.
        std::uint64_t fileOffset{};    // File entries only; rawWordOffset * 4 bytes.
        std::uint32_t fileSize{};      // File entries only; size in bytes.
    };

    struct Limits {
        std::size_t maxEntries{1'000'000U};
        std::size_t maxStringTableBytes{64U * 1024U * 1024U};
        std::size_t maxFstBytes{256U * 1024U * 1024U};
        std::size_t maxDepth{256U};
    };

    [[nodiscard]] static FstArchive parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static FstArchive parse(std::span<const std::uint8_t> bytes, Limits limits);

    [[nodiscard]] std::span<const Entry> entries() const noexcept { return entries_; }
    [[nodiscard]] const Entry* find(std::string_view relativePath) const;
    // Returns the first non-empty file range containing this byte offset, if any.
    [[nodiscard]] const Entry* findFileContainingDiscOffset(std::uint64_t byteOffset) const noexcept;
    [[nodiscard]] std::string_view rootName() const noexcept { return rootName_; }
    [[nodiscard]] std::size_t fstSize() const noexcept { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::size_t> paths_;
    std::string rootName_;
};

} // namespace smg2::data
