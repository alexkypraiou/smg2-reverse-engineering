#include "smg2/data/u8.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace smg2::data {
namespace {

constexpr std::size_t kU8HeaderSize = 0x20U;
constexpr std::size_t kNodeSize = 0x0CU;
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
    if (offset >= strings.size()) throw ParseError("U8 node name offset is invalid");
    const auto begin = strings.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, strings.end(), 0U);
    if (end == strings.end()) throw ParseError("U8 node name is not terminated");
    if (!allowEmpty && end == begin) throw ParseError("U8 node name is empty");
    return std::string(begin, end);
}

struct Node {
    std::uint32_t typeAndName{};
    std::uint32_t parentOrDataOffset{};
    std::uint32_t endIndexOrSize{};
};

} // namespace

U8Archive U8Archive::parse(std::span<const std::uint8_t> bytes) {
    return parse(bytes, Limits{});
}

U8Archive U8Archive::parse(std::span<const std::uint8_t> source, Limits limits) {
    if (source.size() < kU8HeaderSize) throw ParseError("U8 header is truncated");
    if (readBe32(source, 0U) != 0x55AA382DU) throw ParseError("U8 signature is missing");

    const std::size_t rootOffset = readBe32(source, 4U);
    const std::size_t headerSize = readBe32(source, 8U);
    const std::size_t dataOffset = readBe32(source, 0x0CU);
    if (source.size() > limits.maxArchiveBytes) throw ParseError("U8 archive exceeds configured size limit");
    if (rootOffset < kU8HeaderSize || !hasRange(source.size(), rootOffset, kNodeSize)) {
        throw ParseError("U8 root node is outside the archive");
    }
    if (headerSize < kNodeSize || !hasRange(source.size(), rootOffset, headerSize) ||
        dataOffset < rootOffset + headerSize || dataOffset > source.size()) {
        throw ParseError("U8 header or data offsets are invalid");
    }

    const std::uint32_t rootTypeAndName = readBe32(source, rootOffset);
    const std::uint32_t nodeCount = readBe32(source, rootOffset + 8U);
    if ((rootTypeAndName >> 24U) != kDirectoryType || nodeCount == 0U ||
        nodeCount > limits.maxEntries) {
        throw ParseError("U8 root node type or entry count is invalid");
    }
    const std::size_t nodeBytes = static_cast<std::size_t>(nodeCount) * kNodeSize;
    if (!hasRange(headerSize, 0U, nodeBytes)) throw ParseError("U8 node table exceeds its header");
    const std::size_t stringOffset = rootOffset + nodeBytes;
    const std::size_t headerEnd = rootOffset + headerSize;
    if (stringOffset > headerEnd) throw ParseError("U8 string table begins beyond its header");
    const auto strings = source.subspan(stringOffset, headerEnd - stringOffset);

    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    for (std::size_t i = 0U; i < nodeCount; ++i) {
        const std::size_t at = rootOffset + i * kNodeSize;
        nodes.push_back(Node{readBe32(source, at), readBe32(source, at + 4U),
                             readBe32(source, at + 8U)});
    }
    const std::uint32_t rootNameOffset = rootTypeAndName & 0x00FFFFFFU;
    const std::string rootName = readName(strings, rootNameOffset, true);
    if (nodes[0].parentOrDataOffset != 0U || nodes[0].endIndexOrSize != nodeCount) {
        throw ParseError("U8 root node parent or subtree boundary is invalid");
    }

    U8Archive archive;
    archive.bytes_.assign(source.begin(), source.end());
    archive.rootName_ = rootName;
    archive.entries_.reserve(nodeCount);
    archive.entries_.push_back(Entry{"", rootName, EntryKind::directory, 0U, 0U});
    archive.paths_.emplace("", 0U);

    struct DirectoryFrame {
        std::size_t nodeIndex;
        std::size_t endIndex;
        std::string path;
        std::size_t depth;
    };
    std::vector<DirectoryFrame> stack;
    stack.push_back(DirectoryFrame{0U, nodeCount, "", 0U});

    for (std::size_t i = 1U; i < nodeCount; ++i) {
        while (!stack.empty() && i >= stack.back().endIndex) stack.pop_back();
        if (stack.empty()) throw ParseError("U8 node is outside the root directory subtree");
        const Node& node = nodes[i];
        const std::size_t type = node.typeAndName >> 24U;
        const std::size_t nameOffset = node.typeAndName & 0x00FFFFFFU;
        const std::string name = readName(strings, nameOffset, false);
        if (name == "." || name == ".." || name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) {
            throw ParseError("U8 node name is not a safe path component");
        }
        const std::string path = stack.back().path.empty() ? name : stack.back().path + "/" + name;

        if (type == kDirectoryType) {
            if (node.parentOrDataOffset != stack.back().nodeIndex) {
                throw ParseError("U8 directory parent index does not match traversal");
            }
            const std::size_t endIndex = node.endIndexOrSize;
            if (endIndex <= i || endIndex > stack.back().endIndex) {
                throw ParseError("U8 directory subtree boundary is invalid");
            }
            const std::size_t depth = stack.back().depth + 1U;
            if (depth > limits.maxDepth) throw ParseError("U8 directory tree exceeds configured depth limit");
            if (!archive.paths_.emplace(path, archive.entries_.size()).second) {
                throw ParseError("U8 contains duplicate paths");
            }
            archive.entries_.push_back(Entry{path, name, EntryKind::directory, 0U, 0U});
            stack.push_back(DirectoryFrame{i, endIndex, path, depth});
        } else if (type == 0U) {
            const std::size_t payloadOffset = node.parentOrDataOffset;
            const std::size_t payloadSize = node.endIndexOrSize;
            if (payloadOffset < dataOffset || !hasRange(archive.bytes_.size(), payloadOffset, payloadSize)) {
                throw ParseError("U8 file payload lies outside the archive data section");
            }
            if (!archive.paths_.emplace(path, archive.entries_.size()).second) {
                throw ParseError("U8 contains duplicate paths");
            }
            archive.entries_.push_back(Entry{path, name, EntryKind::file,
                static_cast<std::uint32_t>(payloadSize), payloadOffset});
        } else {
            throw ParseError("U8 node uses an unsupported type");
        }
    }
    return archive;
}

const U8Archive::Entry* U8Archive::find(std::string_view relativePath) const {
    while (!relativePath.empty() && relativePath.front() == '/') relativePath.remove_prefix(1U);
    const auto it = paths_.find(std::string(relativePath));
    return it == paths_.end() ? nullptr : &entries_[it->second];
}

std::span<const std::uint8_t> U8Archive::readFile(std::string_view relativePath) const {
    const Entry* entry = find(relativePath);
    if (entry == nullptr) throw ParseError("U8 path was not found");
    return readFile(*entry);
}

std::span<const std::uint8_t> U8Archive::readFile(const Entry& entry) const {
    if (entry.kind != EntryKind::file) throw ParseError("U8 path names a directory, not a file");
    if (!hasRange(bytes_.size(), entry.dataOffset, entry.storedSize)) {
        throw ParseError("U8 owned file payload lies outside the archive");
    }
    return std::span<const std::uint8_t>(bytes_).subspan(entry.dataOffset, entry.storedSize);
}

} // namespace smg2::data
