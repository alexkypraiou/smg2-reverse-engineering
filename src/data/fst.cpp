#include "smg2/data/fst.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace smg2::data {
namespace {

constexpr std::size_t kFstEntrySize = 0x0CU;
constexpr std::uint32_t kDirectoryType = 1U;

std::uint32_t readBe32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

bool hasRange(std::size_t total, std::size_t offset, std::size_t length) noexcept {
    return offset <= total && length <= total - offset;
}

std::string readName(std::span<const std::uint8_t> strings, std::size_t offset,
                     bool allowEmpty) {
    if (offset >= strings.size()) throw ParseError("FST name offset is outside the string table");
    const auto begin = strings.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, strings.end(), 0U);
    if (end == strings.end()) throw ParseError("FST name is not NUL-terminated");
    if (!allowEmpty && end == begin) throw ParseError("FST path component is empty");
    return std::string(begin, end);
}

struct RawEntry {
    std::uint32_t typeAndName{};
    std::uint32_t parentOrWordOffset{};
    std::uint32_t nextIndexOrSize{};
};

} // namespace

FstArchive FstArchive::parse(std::span<const std::uint8_t> bytes) {
    return parse(bytes, Limits{});
}

FstArchive FstArchive::parse(std::span<const std::uint8_t> source, Limits limits) {
    if (source.size() < kFstEntrySize || source.size() > limits.maxFstBytes) {
        throw ParseError("FST is truncated or exceeds configured size limit");
    }

    // Nintendo RVL SDK evidence for scaling:
    // - RVL_SDK/dvd/dvdfs.c stores FSTEntry.filePosition directly in startAddr
    //   after >> __DVDLayoutFormat; dvd.c initializes that value to 0.
    // - dvdfs.c passes per-file byte offsets as (offset >> 2), and
    //   dvd_broadway.c names DVDLowRead's address parameter wordOffset.
    // Therefore the FST address and DVDLowRead address are 32-bit-word units;
    // this host API converts them to bytes by multiplying by four.
    const std::uint32_t rootWord = readBe32(source, 0U);
    const std::uint32_t entryCount = readBe32(source, 8U);
    if ((rootWord >> 24U) != kDirectoryType || entryCount == 0U ||
        entryCount > limits.maxEntries ||
        static_cast<std::size_t>(entryCount) > source.size() / kFstEntrySize) {
        throw ParseError("FST root type or entry count is invalid");
    }

    const std::size_t tableBytes = static_cast<std::size_t>(entryCount) * kFstEntrySize;
    if (!hasRange(source.size(), 0U, tableBytes)) throw ParseError("FST node table is truncated");
    const std::size_t stringBytes = source.size() - tableBytes;
    if (stringBytes == 0U || stringBytes > limits.maxStringTableBytes) {
        throw ParseError("FST string table is empty or exceeds configured size limit");
    }
    const auto strings = source.subspan(tableBytes, stringBytes);

    std::vector<RawEntry> raw;
    raw.reserve(entryCount);
    for (std::size_t i = 0U; i < entryCount; ++i) {
        const std::size_t at = i * kFstEntrySize;
        raw.push_back(RawEntry{readBe32(source, at), readBe32(source, at + 4U),
                               readBe32(source, at + 8U)});
    }

    const RawEntry& root = raw.front();
    if (root.parentOrWordOffset != 0U || root.nextIndexOrSize != entryCount) {
        throw ParseError("FST root parent or subtree boundary is invalid");
    }
    const std::string rootName = readName(strings, root.typeAndName & 0x00FFFFFFU, true);

    FstArchive archive;
    archive.bytes_.assign(source.begin(), source.end());
    archive.rootName_ = rootName;
    archive.entries_.reserve(entryCount);
    archive.paths_.reserve(entryCount);
    archive.entries_.push_back(Entry{0U, 0U, entryCount, "", rootName,
                                     EntryKind::directory, 0U, 0U, 0U});
    archive.paths_.emplace("", 0U);

    struct DirectoryFrame {
        std::uint32_t index;
        std::uint32_t endIndex;
        std::string path;
        std::size_t depth;
    };
    std::vector<DirectoryFrame> stack;
    stack.push_back(DirectoryFrame{0U, entryCount, "", 0U});

    for (std::uint32_t i = 1U; i < entryCount; ++i) {
        while (!stack.empty() && i >= stack.back().endIndex) stack.pop_back();
        if (stack.empty()) throw ParseError("FST entry lies outside the root subtree");
        const RawEntry& entry = raw[i];
        const std::uint32_t type = entry.typeAndName >> 24U;
        const std::uint32_t nameOffset = entry.typeAndName & 0x00FFFFFFU;
        const std::string name = readName(strings, nameOffset, false);
        if (name == "." || name == ".." || name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) {
            throw ParseError("FST name is not a safe path component");
        }
        const std::string path = stack.back().path.empty() ? name : stack.back().path + "/" + name;

        Entry parsed;
        parsed.index = i;
        parsed.parentIndex = stack.back().index;
        parsed.path = path;
        parsed.name = name;
        if (type == kDirectoryType) {
            if (entry.parentOrWordOffset != stack.back().index) {
                throw ParseError("FST directory parent index does not match traversal");
            }
            if (entry.nextIndexOrSize <= i || entry.nextIndexOrSize > stack.back().endIndex) {
                throw ParseError("FST directory subtree boundary is invalid");
            }
            const std::size_t depth = stack.back().depth + 1U;
            if (depth > limits.maxDepth) throw ParseError("FST directory depth exceeds configured limit");
            parsed.kind = EntryKind::directory;
            parsed.subtreeEnd = entry.nextIndexOrSize;
            if (!archive.paths_.emplace(path, archive.entries_.size()).second) {
                throw ParseError("FST contains duplicate paths");
            }
            archive.entries_.push_back(std::move(parsed));
            stack.push_back(DirectoryFrame{i, entry.nextIndexOrSize, path, depth});
        } else if (type == 0U) {
            parsed.kind = EntryKind::file;
            parsed.subtreeEnd = i + 1U;
            parsed.rawWordOffset = entry.parentOrWordOffset;
            parsed.fileOffset = static_cast<std::uint64_t>(entry.parentOrWordOffset) << 2U;
            parsed.fileSize = entry.nextIndexOrSize;
            if (parsed.fileSize > std::numeric_limits<std::uint64_t>::max() - parsed.fileOffset) {
                throw ParseError("FST file range overflows the host offset type");
            }
            if (!archive.paths_.emplace(path, archive.entries_.size()).second) {
                throw ParseError("FST contains duplicate paths");
            }
            archive.entries_.push_back(std::move(parsed));
        } else {
            throw ParseError("FST entry uses an unsupported type byte");
        }
    }
    return archive;
}

const FstArchive::Entry* FstArchive::find(std::string_view relativePath) const {
    while (!relativePath.empty() && relativePath.front() == '/') relativePath.remove_prefix(1U);
    const auto it = paths_.find(std::string(relativePath));
    return it == paths_.end() ? nullptr : &entries_[it->second];
}

const FstArchive::Entry* FstArchive::findFileContainingDiscOffset(std::uint64_t byteOffset) const noexcept {
    for (const Entry& entry : entries_) {
        if (entry.kind != EntryKind::file || entry.fileSize == 0U || byteOffset < entry.fileOffset) continue;
        const std::uint64_t relative = byteOffset - entry.fileOffset;
        if (relative < entry.fileSize) return &entry;
    }
    return nullptr;
}

} // namespace smg2::data
