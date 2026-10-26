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

// Nintendo U8 archive reader. The archive owns its bytes; file spans remain
// valid as long as the archive remains alive.
class U8Archive {
public:
    enum class EntryKind : std::uint8_t { directory, file };

    struct Entry {
        std::string path; // Relative to archive root; root itself has an empty path.
        std::string name;
        EntryKind kind{};
        std::uint32_t storedSize{};
        std::size_t dataOffset{}; // Absolute byte offset in the owned archive.
    };

    struct Limits {
        std::size_t maxEntries{1'000'000U};
        std::size_t maxArchiveBytes{1024U * 1024U * 1024U};
        std::size_t maxDepth{256U};
    };

    [[nodiscard]] static U8Archive parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static U8Archive parse(std::span<const std::uint8_t> bytes, Limits limits);

    [[nodiscard]] std::span<const Entry> entries() const noexcept { return entries_; }
    [[nodiscard]] const Entry* find(std::string_view relativePath) const;
    [[nodiscard]] std::span<const std::uint8_t> readFile(std::string_view relativePath) const;
    [[nodiscard]] std::span<const std::uint8_t> readFile(const Entry& entry) const;
    [[nodiscard]] std::string_view rootName() const noexcept { return rootName_; }
    [[nodiscard]] std::size_t archiveSize() const noexcept { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::size_t> paths_;
    std::string rootName_;
};

} // namespace smg2::data
