#pragma once

#include "smg2/data/yaz0.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace smg2::data {

// Bounds-checked J3D BMD/BDL section container reader. It exposes encoded GX
// arrays and section payloads; it does not decode SHP1 display lists/materials.
class J3dModel {
public:
    struct Section {
        std::string tag;
        std::size_t offset{}; // Absolute offset in the owned model.
        std::uint32_t size{};
    };

    struct ModelMetadata {
        std::optional<std::uint16_t> infoFlags;
        std::optional<std::uint32_t> packetCount;
        std::optional<std::uint32_t> vertexCount;
        std::optional<std::uint16_t> envelopeMatrixCount;
        std::optional<std::uint16_t> drawMatrixCount;
        std::optional<std::uint16_t> jointCount;
        std::optional<std::uint16_t> shapeCount;
        std::optional<std::uint16_t> materialCount;
        std::optional<std::uint16_t> textureCount;
    };

    struct VertexFormat {
        std::uint32_t attribute{}; // GX_VA_* value; 0xff terminates the list.
        std::uint32_t componentCount{};
        std::uint32_t componentType{};
        std::uint8_t fractionalBits{};
    };

    struct VertexArray {
        std::uint32_t attribute{};
        std::uint32_t sectionRelativeOffset{};
        std::optional<VertexFormat> format;
        std::size_t elementStride{}; // Known for the position attribute.
        std::size_t encodedSize{};   // Known for positions when INF1 has vtxNum.
    };

    struct Limits {
        std::size_t maxModelBytes{512U * 1024U * 1024U};
        std::uint32_t maxSections{256U};
        std::size_t maxVertexFormats{256U};
    };

    [[nodiscard]] static J3dModel parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static J3dModel parse(std::span<const std::uint8_t> bytes, Limits limits);

    [[nodiscard]] std::string_view magic() const noexcept { return magic_; }
    [[nodiscard]] std::string_view format() const noexcept { return format_; }
    [[nodiscard]] std::uint32_t declaredSize() const noexcept { return declaredSize_; }
    [[nodiscard]] std::uint32_t sectionCount() const noexcept { return static_cast<std::uint32_t>(sections_.size()); }
    [[nodiscard]] std::span<const Section> sections() const noexcept { return sections_; }
    [[nodiscard]] const Section* findSection(std::string_view tag) const noexcept;
    // Returns the section payload without its 8-byte tag/size prefix.
    [[nodiscard]] std::span<const std::uint8_t> sectionPayload(const Section& section) const;
    [[nodiscard]] std::span<const std::uint8_t> sectionPayload(std::string_view tag) const;
    [[nodiscard]] const ModelMetadata& metadata() const noexcept { return metadata_; }
    [[nodiscard]] std::span<const VertexFormat> vertexFormats() const noexcept { return vertexFormats_; }
    [[nodiscard]] std::span<const VertexArray> vertexArrays() const noexcept { return vertexArrays_; }
    [[nodiscard]] const VertexArray* positionArray() const noexcept;
    // Encoded position bytes in GX component representation; no conversion is performed.
    [[nodiscard]] std::span<const std::uint8_t> encodedPositions() const;

private:
    std::vector<std::uint8_t> bytes_;
    std::string magic_;
    std::string format_;
    std::uint32_t declaredSize_{};
    std::vector<Section> sections_;
    ModelMetadata metadata_;
    std::vector<VertexFormat> vertexFormats_;
    std::vector<VertexArray> vertexArrays_;
    std::optional<std::size_t> positionDataOffset_;
    std::size_t positionDataSize_{};
};

} // namespace smg2::data
