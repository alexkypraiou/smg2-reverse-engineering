#include "smg2/data/rarc.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

namespace smg2::data {
namespace {

constexpr std::size_t kRarcHeaderSize = 0x20U;
constexpr std::size_t kInfoHeaderSize = 0x20U;
constexpr std::size_t kNodeSize = 0x10U;
constexpr std::size_t kFileEntrySize = 0x14U;
constexpr std::uint8_t kDirectoryFlag = 0x02U;
constexpr std::uint8_t kFileFlag = 0x01U;

std::uint16_t readBe16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                      static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::uint32_t readBe32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

bool hasRange(std::size_t total, std::size_t offset, std::size_t length) noexcept {
    return offset <= total && length <= total - offset;
}

void requireRange(std::size_t total, std::size_t offset, std::size_t length,
                  std::string_view what) {
    if (!hasRange(total, offset, length)) {
        throw ParseError(std::string("RARC ") + std::string(what) + " lies outside its section");
    }
}

std::string readCString(std::span<const std::uint8_t> table, std::size_t offset,
                        std::string_view what) {
    if (offset >= table.size()) throw ParseError(std::string("RARC ") + std::string(what) + " name offset is invalid");
    const auto begin = table.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, table.end(), 0U);
    if (end == table.end()) throw ParseError(std::string("RARC ") + std::string(what) + " name is not terminated");
    if (end == begin) throw ParseError(std::string("RARC ") + std::string(what) + " name is empty");
    return std::string(begin, end);
}

} // namespace

RarcArchive RarcArchive::parse(std::span<const std::uint8_t> bytes) {
    return parse(bytes, Limits{});
}

RarcArchive RarcArchive::parse(std::span<const std::uint8_t> source, Limits limits) {
    if (source.size() < kRarcHeaderSize) throw ParseError("RARC header is truncated");
    if (source[0] != 'R' || source[1] != 'A' || source[2] != 'R' || source[3] != 'C') {
        throw ParseError("RARC signature is missing");
    }

    const std::size_t declaredSize = readBe32(source, 4U);
    const std::size_t headerSize = readBe32(source, 8U);
    const std::size_t fileDataOffset = readBe32(source, 0x0CU);
    const std::size_t totalDataSize = readBe32(source, 0x10U);
    const std::size_t mramDataSize = readBe32(source, 0x14U);
    const std::size_t aramDataSize = readBe32(source, 0x18U);
    if (headerSize != kRarcHeaderSize) throw ParseError("RARC header size is not 0x20");
    if (declaredSize < kRarcHeaderSize || declaredSize > source.size()) {
        throw ParseError("RARC declared file size is outside the input buffer");
    }
    if (declaredSize > limits.maxArchiveBytes) throw ParseError("RARC archive exceeds configured size limit");
    if (fileDataOffset < kInfoHeaderSize || !hasRange(declaredSize, kRarcHeaderSize, fileDataOffset)) {
        throw ParseError("RARC file-data base is outside the archive");
    }
    if (mramDataSize > totalDataSize || aramDataSize > totalDataSize - mramDataSize) {
        throw ParseError("RARC memory data sizes exceed total data size");
    }

    const std::size_t infoOffset = kRarcHeaderSize;
    const std::size_t infoSize = fileDataOffset;
    requireRange(declaredSize, infoOffset, infoSize, "INFO section");
    const auto info = source.subspan(infoOffset, infoSize);
    if (info.size() < kInfoHeaderSize) throw ParseError("RARC INFO block is truncated");

    const std::size_t nodeCount = readBe32(info, 0U);
    const std::size_t nodeOffset = readBe32(info, 4U);
    const std::size_t fileCount = readBe32(info, 8U);
    const std::size_t fileOffset = readBe32(info, 0x0CU);
    const std::size_t stringSize = readBe32(info, 0x10U);
    const std::size_t stringOffset = readBe32(info, 0x14U);
    if (nodeCount == 0U || nodeCount > limits.maxEntries || fileCount > limits.maxEntries) {
        throw ParseError("RARC node or entry count is invalid");
    }
    if (stringSize > limits.maxStringTableBytes) throw ParseError("RARC string table exceeds configured size limit");
    if (nodeOffset < kInfoHeaderSize || fileOffset < kInfoHeaderSize || stringOffset < kInfoHeaderSize) {
        throw ParseError("RARC INFO table overlaps its header");
    }
    if (nodeCount > (std::numeric_limits<std::size_t>::max() / kNodeSize) ||
        fileCount > (std::numeric_limits<std::size_t>::max() / kFileEntrySize)) {
        throw ParseError("RARC table size overflows the host size type");
    }
    requireRange(info.size(), nodeOffset, nodeCount * kNodeSize, "node table");
    requireRange(info.size(), fileOffset, fileCount * kFileEntrySize, "file table");
    requireRange(info.size(), stringOffset, stringSize, "string table");

    const auto strings = info.subspan(stringOffset, stringSize);
    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    for (std::size_t i = 0U; i < nodeCount; ++i) {
        const std::size_t at = nodeOffset + i * kNodeSize;
        Node node;
        node.nameOffset = readBe32(info, at + 4U);
        node.entryCount = readBe16(info, at + 0x0AU);
        node.firstEntry = readBe32(info, at + 0x0CU);
        if (node.firstEntry > fileCount || node.entryCount > fileCount - node.firstEntry) {
            throw ParseError("RARC node references entries outside the file table");
        }
        (void)readCString(strings, node.nameOffset, "directory");
        nodes.push_back(node);
    }

    std::vector<FileRecord> files;
    files.reserve(fileCount);
    for (std::size_t i = 0U; i < fileCount; ++i) {
        const std::size_t at = fileOffset + i * kFileEntrySize;
        FileRecord file;
        file.id = readBe16(info, at);
        // SDIFileEntry stores a one-byte attribute field followed by a 24-bit
        // string-table offset. Attribute storage bits (MRAM/ARAM/DVD) and the
        // Yaz0/compressed bits coexist with FILE_FLAG_FILE.
        file.flags = info[at + 4U];
        file.nameOffset = (static_cast<std::uint32_t>(info[at + 5U]) << 16U) |
                          (static_cast<std::uint32_t>(info[at + 6U]) << 8U) |
                          static_cast<std::uint32_t>(info[at + 7U]);
        file.dataOffset = readBe32(info, at + 8U);
        file.dataSize = readBe32(info, at + 0x0CU);
        (void)readBe32(info, at + 0x10U); // Runtime-specific RARC field is reserved here.
        (void)readCString(strings, file.nameOffset, "file-entry");
        files.push_back(file);
    }

    RarcArchive archive;
    archive.bytes_.assign(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(declaredSize));
    archive.rootName_ = readCString(strings, nodes.front().nameOffset, "root directory");
    if (archive.rootName_ == "." || archive.rootName_ == "..") throw ParseError("RARC root directory has an invalid name");
    archive.entries_.reserve(std::min(limits.maxEntries, nodeCount + fileCount));
    std::vector<bool> seenNodes(nodeCount, false);
    std::size_t visitedEntryCount = 0U;
    const std::size_t dataBase = kRarcHeaderSize + fileDataOffset;
    const std::size_t availableData = declaredSize - dataBase;
    const std::size_t reportedData = totalDataSize == 0U ? availableData : totalDataSize;
    if (reportedData > availableData) throw ParseError("RARC file-data section exceeds the archive");

    auto addPath = [&archive](Entry entry) {
        if (!archive.paths_.emplace(entry.path, archive.entries_.size()).second) {
            throw ParseError("RARC contains duplicate paths");
        }
        archive.entries_.push_back(std::move(entry));
    };

    std::function<void(std::size_t, std::string, std::size_t)> walk;
    walk = [&](std::size_t nodeIndex, std::string parent, std::size_t depth) {
        if (depth > limits.maxDepth) throw ParseError("RARC directory tree exceeds configured depth limit");
        if (nodeIndex >= nodes.size()) throw ParseError("RARC directory entry references a missing node");
        if (seenNodes[nodeIndex]) throw ParseError("RARC directory graph contains a cycle or multiply linked directory");
        seenNodes[nodeIndex] = true;
        const Node& node = nodes[nodeIndex];
        for (std::size_t local = 0U; local < node.entryCount; ++local) {
            if (++visitedEntryCount > limits.maxEntries) throw ParseError("RARC traversal exceeds configured entry limit");
            const FileRecord& record = files[node.firstEntry + local];
            const std::string name = readCString(strings, record.nameOffset, "entry");
            if (name == "." || name == "..") continue;
            if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
                throw ParseError("RARC entry name contains a path separator");
            }
            const std::string path = parent.empty() ? name : parent + "/" + name;

            if ((record.flags & kDirectoryFlag) != 0U && (record.flags & kFileFlag) == 0U) {
                if (record.dataOffset >= nodes.size()) {
                    throw ParseError("RARC directory entry has invalid node metadata");
                }
                Entry entry{path, name, EntryKind::directory, record.id, record.flags, 0U, 0U};
                addPath(std::move(entry));
                walk(record.dataOffset, path, depth + 1U);
            } else if ((record.flags & kFileFlag) != 0U && (record.flags & kDirectoryFlag) == 0U) {
                const std::size_t offset = dataBase + static_cast<std::size_t>(record.dataOffset);
                if (record.dataOffset > reportedData || record.dataSize > reportedData - record.dataOffset) {
                    throw ParseError("RARC file payload points outside the data section");
                }
                requireRange(declaredSize, offset, record.dataSize, "file payload");
                addPath(Entry{path, name, EntryKind::file, record.id, record.flags,
                              record.dataSize, offset});
            } else {
                throw ParseError("RARC entry uses an unsupported type flag");
            }
        }
    };

    addPath(Entry{"", archive.rootName_, EntryKind::directory, 0U, kDirectoryFlag, 0U, 0U});
    walk(0U, "", 0U);
    return archive;
}

const RarcArchive::Entry* RarcArchive::find(std::string_view relativePath) const {
    while (!relativePath.empty() && relativePath.front() == '/') relativePath.remove_prefix(1U);
    const auto it = paths_.find(std::string(relativePath));
    return it == paths_.end() ? nullptr : &entries_[it->second];
}

std::span<const std::uint8_t> RarcArchive::readFile(std::string_view relativePath) const {
    const Entry* entry = find(relativePath);
    if (entry == nullptr) throw ParseError("RARC path was not found");
    return readFile(*entry);
}

std::span<const std::uint8_t> RarcArchive::readFile(const Entry& entry) const {
    if (entry.kind != EntryKind::file) throw ParseError("RARC path names a directory, not a file");
    requireRange(bytes_.size(), entry.dataOffset, entry.storedSize, "owned file payload");
    return std::span<const std::uint8_t>(bytes_).subspan(entry.dataOffset, entry.storedSize);
}

} // namespace smg2::data
