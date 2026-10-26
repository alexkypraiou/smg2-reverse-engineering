#include "smg2/data/j3d.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace smg2::data {
namespace {

bool hasRange(std::size_t total, std::size_t offset, std::size_t length) noexcept {
    return offset <= total && length <= total - offset;
}

std::uint16_t be16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[at]) << 8U) |
                                      static_cast<std::uint16_t>(bytes[at + 1U]));
}

std::uint32_t be32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[at + 3U]);
}

std::string stringAt(std::span<const std::uint8_t> bytes, std::size_t at, std::size_t size) {
    if (!hasRange(bytes.size(), at, size)) throw ParseError("J3D section header is truncated");
    return std::string(reinterpret_cast<const char*>(bytes.data() + at), size);
}

std::size_t positionComponentWidth(std::uint32_t type) {
    switch (type) {
    case 0U: case 1U: return 1U; // GX_U8 / GX_S8
    case 2U: case 3U: return 2U; // GX_U16 / GX_S16
    case 4U: return 4U;          // GX_F32
    default: throw ParseError("J3D position format uses an unsupported component type");
    }
}

} // namespace

J3dModel J3dModel::parse(std::span<const std::uint8_t> bytes) {
    return parse(bytes, Limits{});
}

J3dModel J3dModel::parse(std::span<const std::uint8_t> source, Limits limits) {
    if (source.size() < 0x20U || source.size() > limits.maxModelBytes)
        throw ParseError("J3D model is truncated or exceeds configured size limit");
    J3dModel model;
    model.magic_ = stringAt(source, 0U, 4U);
    if (model.magic_ != "J3D1" && model.magic_ != "J3D2")
        throw ParseError("J3D model has an unsupported container magic");
    model.format_ = stringAt(source, 4U, 4U);
    if (model.format_.compare(0U, 3U, "bmd") != 0 && model.format_.compare(0U, 3U, "bdl") != 0)
        throw ParseError("J3D container is not a BMD or BDL model");
    model.declaredSize_ = be32(source, 8U);
    const std::uint32_t count = be32(source, 0x0CU);
    if (model.declaredSize_ < 0x20U || model.declaredSize_ > source.size())
        throw ParseError("J3D declared size is outside the supplied model bytes");
    if (count > limits.maxSections || static_cast<std::size_t>(count) > (model.declaredSize_ - 0x20U) / 8U)
        throw ParseError("J3D section count exceeds limits or cannot fit in the model");

    model.bytes_.assign(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(model.declaredSize_));
    const auto owned = std::span<const std::uint8_t>(model.bytes_);
    model.sections_.reserve(count);
    std::size_t cursor = 0x20U;
    for (std::uint32_t i = 0U; i < count; ++i) {
        if (!hasRange(model.declaredSize_, cursor, 8U)) throw ParseError("J3D section header is truncated");
        const std::uint32_t sectionSize = be32(owned, cursor + 4U);
        if (sectionSize < 8U || !hasRange(model.declaredSize_, cursor, sectionSize))
            throw ParseError("J3D section size is invalid or exceeds the model");
        model.sections_.push_back(Section{stringAt(owned, cursor, 4U), cursor, sectionSize});
        cursor += sectionSize;
    }
    if (cursor != model.declaredSize_) throw ParseError("J3D section table does not consume the declared model size");

    const auto setCount = [&](std::string_view tag, std::size_t minSize,
                              std::optional<std::uint16_t>& output) {
        const Section* section = model.findSection(tag);
        if (!section) return;
        if (section->size < minSize) throw ParseError("J3D structural section is too short: " + std::string(tag));
        output = be16(owned, section->offset + 8U);
    };
    if (const Section* info = model.findSection("INF1")) {
        if (info->size < 0x18U) throw ParseError("J3D INF1 section is too short");
        model.metadata_.infoFlags = be16(owned, info->offset + 8U);
        model.metadata_.packetCount = be32(owned, info->offset + 0x0CU);
        model.metadata_.vertexCount = be32(owned, info->offset + 0x10U);
        const std::uint32_t hierarchy = be32(owned, info->offset + 0x14U);
        if (hierarchy != 0U && (hierarchy < 0x18U || hierarchy >= info->size))
            throw ParseError("J3D INF1 hierarchy offset is outside its section");
    }
    setCount("EVP1", 0x0AU, model.metadata_.envelopeMatrixCount);
    setCount("DRW1", 0x0AU, model.metadata_.drawMatrixCount);
    setCount("JNT1", 0x0AU, model.metadata_.jointCount);
    setCount("SHP1", 0x0AU, model.metadata_.shapeCount);
    setCount("MAT3", 0x0AU, model.metadata_.materialCount);
    if (!model.metadata_.materialCount) setCount("MAT2", 0x0AU, model.metadata_.materialCount);
    setCount("TEX1", 0x0AU, model.metadata_.textureCount);

    if (const Section* vertex = model.findSection("VTX1")) {
        if (vertex->size < 0x40U) throw ParseError("J3D VTX1 section is too short for its array directory");
        const std::uint32_t formatsOffset = be32(owned, vertex->offset + 8U);
        std::array<std::uint32_t, 13U> arrayOffsets{};
        for (std::size_t i = 0U; i < arrayOffsets.size(); ++i) {
            arrayOffsets[i] = be32(owned, vertex->offset + 0x0CU + i * 4U);
            if (arrayOffsets[i] != 0U && (arrayOffsets[i] < 0x40U || arrayOffsets[i] >= vertex->size))
                throw ParseError("J3D VTX1 array offset is outside its section");
        }
        if (formatsOffset != 0U) {
            if (formatsOffset < 0x40U || formatsOffset >= vertex->size)
                throw ParseError("J3D VTX1 format-list offset is outside its section");
            bool terminated = false;
            for (std::size_t i = 0U; i < limits.maxVertexFormats; ++i) {
                const std::size_t at = static_cast<std::size_t>(formatsOffset) + i * 16U;
                if (!hasRange(vertex->size, at, 16U)) throw ParseError("J3D VTX1 format list is unterminated");
                VertexFormat format{be32(owned, vertex->offset + at), be32(owned, vertex->offset + at + 4U),
                                    be32(owned, vertex->offset + at + 8U), owned[vertex->offset + at + 12U]};
                if (format.attribute == 0xFFU) { terminated = true; break; }
                if (format.attribute > 0xFFU ||
                    (format.attribute == 9U && (format.componentCount > 1U || format.componentType > 4U)))
                    throw ParseError("J3D VTX1 contains an invalid position format descriptor");
                model.vertexFormats_.push_back(format);
            }
            if (!terminated) throw ParseError("J3D VTX1 format list exceeds the configured descriptor limit");
        }
        for (std::size_t i = 0U; i < arrayOffsets.size(); ++i) {
            if (arrayOffsets[i] == 0U) continue;
            VertexArray array;
            array.attribute = i == 0U ? 9U : (i == 1U ? 10U : (i == 2U ? 25U : (i == 3U ? 26U : (i < 6U ? 11U + static_cast<std::uint32_t>(i - 4U) : 13U + static_cast<std::uint32_t>(i - 6U)))));
            array.sectionRelativeOffset = arrayOffsets[i];
            const auto format = std::find_if(model.vertexFormats_.begin(), model.vertexFormats_.end(),
                [&](const VertexFormat& item) { return item.attribute == array.attribute; });
            if (format != model.vertexFormats_.end()) array.format = *format;
            if (i == 0U && array.format) {
                const std::size_t components = array.format->componentCount == 0U ? 2U : 3U;
                array.elementStride = components * positionComponentWidth(array.format->componentType);
                if (model.metadata_.vertexCount) {
                    const std::uint64_t encoded = static_cast<std::uint64_t>(array.elementStride) * *model.metadata_.vertexCount;
                    if (encoded > std::numeric_limits<std::size_t>::max() ||
                        !hasRange(vertex->size, array.sectionRelativeOffset, static_cast<std::size_t>(encoded)))
                        throw ParseError("J3D VTX1 position data exceeds its section");
                    array.encodedSize = static_cast<std::size_t>(encoded);
                    model.positionDataOffset_ = vertex->offset + array.sectionRelativeOffset;
                    model.positionDataSize_ = array.encodedSize;
                }
            }
            model.vertexArrays_.push_back(std::move(array));
        }
    }
    return model;
}

const J3dModel::Section* J3dModel::findSection(std::string_view tag) const noexcept {
    const auto found = std::find_if(sections_.begin(), sections_.end(),
        [&](const Section& section) { return section.tag == tag; });
    return found == sections_.end() ? nullptr : &*found;
}

std::span<const std::uint8_t> J3dModel::sectionPayload(const Section& section) const {
    if (!hasRange(bytes_.size(), section.offset, section.size) || section.size < 8U)
        throw ParseError("J3D section handle does not belong to this model");
    return std::span<const std::uint8_t>(bytes_).subspan(section.offset + 8U, section.size - 8U);
}

std::span<const std::uint8_t> J3dModel::sectionPayload(std::string_view tag) const {
    const Section* section = findSection(tag);
    return section ? sectionPayload(*section) : std::span<const std::uint8_t>{};
}

const J3dModel::VertexArray* J3dModel::positionArray() const noexcept {
    const auto found = std::find_if(vertexArrays_.begin(), vertexArrays_.end(),
        [](const VertexArray& array) { return array.attribute == 9U; });
    return found == vertexArrays_.end() ? nullptr : &*found;
}

std::span<const std::uint8_t> J3dModel::encodedPositions() const {
    if (!positionDataOffset_) return {};
    if (!hasRange(bytes_.size(), *positionDataOffset_, positionDataSize_))
        throw ParseError("J3D position data handle is invalid");
    return std::span<const std::uint8_t>(bytes_).subspan(*positionDataOffset_, positionDataSize_);
}

} // namespace smg2::data
