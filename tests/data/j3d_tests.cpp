#include "smg2/data/j3d.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using smg2::data::J3dModel;
using smg2::data::ParseError;
unsigned checks = 0U;

void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error("FAIL: " + message);
}
template<class Fn> void expectError(Fn&& fn, const std::string& message) {
    bool didThrow = false;
    try { fn(); } catch (const ParseError&) { didThrow = true; }
    check(didThrow, message);
}
void be16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b.at(o) = static_cast<std::uint8_t>(v >> 8U); b.at(o + 1U) = static_cast<std::uint8_t>(v);
}
void be32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b.at(o) = static_cast<std::uint8_t>(v >> 24U); b.at(o + 1U) = static_cast<std::uint8_t>(v >> 16U);
    b.at(o + 2U) = static_cast<std::uint8_t>(v >> 8U); b.at(o + 3U) = static_cast<std::uint8_t>(v);
}

std::vector<std::uint8_t> fixture() {
    constexpr std::size_t infOffset = 0x20U;
    constexpr std::size_t infSize = 0x20U;
    constexpr std::size_t vtxOffset = infOffset + infSize;
    constexpr std::size_t vtxSize = 0x80U;
    std::vector<std::uint8_t> b(vtxOffset + vtxSize, 0U);
    b[0] = 'J'; b[1] = '3'; b[2] = 'D'; b[3] = '2';
    b[4] = 'b'; b[5] = 'd'; b[6] = 'l'; b[7] = '4';
    be32(b, 8U, static_cast<std::uint32_t>(b.size())); be32(b, 0x0CU, 2U);
    b[infOffset] = 'I'; b[infOffset + 1U] = 'N'; b[infOffset + 2U] = 'F'; b[infOffset + 3U] = '1';
    be32(b, infOffset + 4U, static_cast<std::uint32_t>(infSize)); be16(b, infOffset + 8U, 0x1234U);
    be32(b, infOffset + 0x0CU, 1U); be32(b, infOffset + 0x10U, 2U); be32(b, infOffset + 0x14U, 0x18U);
    b[vtxOffset] = 'V'; b[vtxOffset + 1U] = 'T'; b[vtxOffset + 2U] = 'X'; b[vtxOffset + 3U] = '1';
    be32(b, vtxOffset + 4U, static_cast<std::uint32_t>(vtxSize));
    be32(b, vtxOffset + 8U, 0x40U); // descriptor list
    be32(b, vtxOffset + 0x0CU, 0x60U); // position array
    be32(b, vtxOffset + 0x40U, 9U); be32(b, vtxOffset + 0x44U, 1U); // XYZ
    be32(b, vtxOffset + 0x48U, 3U); b[vtxOffset + 0x4CU] = 7U; // S16, frac=7
    be32(b, vtxOffset + 0x50U, 0xFFU); // terminator
    for (std::size_t i = 0U; i < 12U; ++i) b[vtxOffset + 0x60U + i] = static_cast<std::uint8_t>(i);
    return b;
}

void testValid() {
    const auto b = fixture();
    const auto model = J3dModel::parse(b);
    check(model.magic() == "J3D2" && model.format() == "bdl4", "header magic and format");
    check(model.declaredSize() == b.size() && model.sectionCount() == 2U, "declared model dimensions");
    check(model.findSection("INF1") && model.sectionPayload("INF1").size() == 24U, "section lookup/payload");
    check(model.metadata().infoFlags == 0x1234U && model.metadata().packetCount == 1U &&
          model.metadata().vertexCount == 2U, "INF1 known fields");
    check(model.vertexFormats().size() == 1U && model.vertexFormats()[0].attribute == 9U &&
          model.vertexFormats()[0].fractionalBits == 7U, "GX vertex format descriptor");
    check(model.positionArray() && model.positionArray()->elementStride == 6U &&
          model.positionArray()->encodedSize == 12U, "XYZ S16 position descriptor and size");
    check(model.encodedPositions().size() == 12U && model.encodedPositions()[11] == 11U,
          "position bytes preserve encoded representation");
}

void testMalformed() {
    auto b = fixture(); b[0] = 'X';
    expectError([&] { (void)J3dModel::parse(b); }, "reject unknown magic");
    b = fixture(); be32(b, 8U, 0xFFFFFFFFU);
    expectError([&] { (void)J3dModel::parse(b); }, "reject declared size past source");
    b = fixture(); be32(b, 0x0CU, 0xFFFFFFFFU);
    expectError([&] { (void)J3dModel::parse(b); }, "reject impossible section count");
    b = fixture(); be32(b, 0x20U + 4U, 7U);
    expectError([&] { (void)J3dModel::parse(b); }, "reject section size below header");
    b = fixture(); be32(b, 0x40U + 0x0CU, 0xFFFFFFFFU);
    expectError([&] { (void)J3dModel::parse(b); }, "reject invalid VTX1 position offset");
    b = fixture(); be32(b, 0x40U + 0x50U, 8U); // replace the descriptor-list terminator
    expectError([&] { (void)J3dModel::parse(b); }, "reject unterminated VTX1 formats");
    b = fixture(); be32(b, 0x20U + 0x14U, 0xFFFFFFFFU);
    expectError([&] { (void)J3dModel::parse(b); }, "reject invalid INF1 hierarchy offset");
}
}

int main() {
    try { testValid(); testMalformed(); std::cout << "PASS: J3D parser (" << checks << " checks)\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
