#include "smg2/gravity/cube_gravity.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

using smg2::gravity::CubeGravity;
using smg2::gravity::CubeGravityFrame;
using smg2::gravity::Vec3f;

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

bool near(float actual, float expected, float epsilon = 1.0e-5F) {
    return std::fabs(actual - expected) <= epsilon;
}

bool nearVec(Vec3f actual, Vec3f expected, float epsilon = 1.0e-5F) {
    return near(actual.x, expected.x, epsilon) && near(actual.y, expected.y, epsilon) &&
           near(actual.z, expected.z, epsilon);
}

Vec3f addScaled(Vec3f origin, Vec3f axis, float amount) {
    return {origin.x + axis.x * amount, origin.y + axis.y * amount,
            origin.z + axis.z * amount};
}

void testAll27Regions() {
    CubeGravityFrame frame{};
    frame.half_length_x = 1.0F;
    frame.half_length_y = 2.0F;
    frame.half_length_z = 3.0F;
    const CubeGravity cube(frame);
    const std::array<float, 3> local{{-1.5F, 0.0F, 1.5F}};
    for (int z = 0; z < 3; ++z) {
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                // The reference divides the projection by mLen and then compares
                // against +/-mLen, so use +/-1.5*mLen^2 for the outside samples.
                const Vec3f position{local[static_cast<std::size_t>(x)] * frame.half_length_x * frame.half_length_x,
                                     local[static_cast<std::size_t>(y)] * frame.half_length_y * frame.half_length_y,
                                     local[static_cast<std::size_t>(z)] * frame.half_length_z * frame.half_length_z};
                const int expected = x + 3 * y + 9 * z;
                check(cube.calcGravityArea(position) == expected, "all 27 encoded regions");
            }
        }
    }
}

void testStrictBoundariesAndFlags() {
    CubeGravityFrame frame{};
    frame.half_length_x = 2.0F;
    frame.half_length_y = 2.0F;
    frame.half_length_z = 2.0F;
    const CubeGravity all(frame);
    check(all.calcGravityArea({-4.0F, 0.0F, 0.0F}) == 13, "negative x exact threshold is inside");
    check(all.calcGravityArea({4.0F, 0.0F, 0.0F}) == 13, "positive x exact threshold is inside");
    check(all.calcGravityArea({-4.001F, 0.0F, 0.0F}) == 12, "negative x strict outside");
    check(all.calcGravityArea({4.001F, 0.0F, 0.0F}) == 14, "positive x strict outside");
    check(all.calcGravityArea({0.0F, -4.0F, 0.0F}) == 13, "negative y exact threshold is inside");
    check(all.calcGravityArea({0.0F, 4.0F, 0.0F}) == 13, "positive y exact threshold is inside");
    check(all.calcGravityArea({0.0F, -4.001F, 0.0F}) == 10, "negative y strict outside");
    check(all.calcGravityArea({0.0F, 4.001F, 0.0F}) == 16, "positive y strict outside");
    check(all.calcGravityArea({0.0F, 0.0F, -4.0F}) == 13, "negative z exact threshold is inside");
    check(all.calcGravityArea({0.0F, 0.0F, 4.0F}) == 13, "positive z exact threshold is inside");
    check(all.calcGravityArea({0.0F, 0.0F, -4.001F}) == 4, "negative z strict outside");
    check(all.calcGravityArea({0.0F, 0.0F, 4.001F}) == 22, "positive z strict outside");

    struct FlagCase { std::uint8_t bit; Vec3f outside; int expected; };
    const std::array<FlagCase, 6> cases{{
        {2U, {-4.5F, 0.0F, 0.0F}, 12}, {1U, {4.5F, 0.0F, 0.0F}, 14},
        {8U, {0.0F, -4.5F, 0.0F}, 10}, {4U, {0.0F, 4.5F, 0.0F}, 16},
        {32U, {0.0F, 0.0F, -4.5F}, 4}, {16U, {0.0F, 0.0F, 4.5F}, 22},
    }};
    for (const auto& c : cases) {
        CubeGravityFrame masked = frame;
        masked.active_faces = static_cast<std::uint8_t>(0x3FU & ~c.bit);
        check(CubeGravity(masked).calcGravityArea(c.outside) == -1, "each disabled face rejects its outside region");
        masked.active_faces = c.bit;
        check(CubeGravity(masked).calcGravityArea(c.outside) == c.expected, "each enabled face admits its outside region");
    }
}

void testRotatedTranslatedFrame() {
    CubeGravityFrame frame{};
    frame.translation = {10.0F, -4.0F, 6.0F};
    frame.basis_x = {0.0F, 1.0F, 0.0F};
    frame.basis_y = {-1.0F, 0.0F, 0.0F};
    frame.basis_z = {0.0F, 0.0F, 1.0F};
    frame.half_length_x = 1.0F;
    frame.half_length_y = 1.0F;
    frame.half_length_z = 1.0F;
    const CubeGravity cube(frame);
    const Vec3f p = addScaled(addScaled(addScaled(frame.translation, frame.basis_x, 1.5F),
                                       frame.basis_y, -1.5F), frame.basis_z, 0.0F);
    check(cube.calcGravityArea(p) == 11, "rotated and translated basis coordinates");
}

void testScaledBasisAndLengths() {
    CubeGravityFrame frame{};
    frame.translation = {-3.0F, 8.0F, 2.0F};
    frame.basis_x = {2.0F, 0.0F, 0.0F};
    frame.basis_y = {0.0F, 3.0F, 0.0F};
    frame.basis_z = {0.0F, 0.0F, 4.0F};
    frame.half_length_x = 2.0F;
    frame.half_length_y = 3.0F;
    frame.half_length_z = 4.0F;
    const CubeGravity cube(frame);

    // With matching basis magnitudes and lengths, projections at ±1.5 lengths
    // land in the expected exterior region in this scaled frame.
    const Vec3f outside = addScaled(addScaled(addScaled(frame.translation, frame.basis_x, 3.0F),
                                              frame.basis_y, -4.5F),
                                    frame.basis_z, 0.0F);
    check(cube.calcGravityArea(outside) == 11, "scaled basis with matching half lengths");
}

void testFaceGravity() {
    CubeGravityFrame frame{};
    frame.translation = {5.0F, 7.0F, 9.0F};
    frame.basis_x = {2.0F, 0.0F, 0.0F};
    frame.basis_y = {0.0F, 3.0F, 0.0F};
    frame.basis_z = {0.0F, 0.0F, 4.0F};
    const CubeGravity cube(frame);
    struct FaceCase { int area; Vec3f basis; float magnitude; };
    const std::array<FaceCase, 6> faces{{
        {12, {1.0F, 0.0F, 0.0F}, 2.0F}, {14, {-1.0F, 0.0F, 0.0F}, 2.0F},
        {10, {0.0F, 1.0F, 0.0F}, 3.0F}, {16, {0.0F, -1.0F, 0.0F}, 3.0F},
        {4, {0.0F, 0.0F, 1.0F}, 4.0F}, {22, {0.0F, 0.0F, -1.0F}, 4.0F},
    }};
    for (const auto& face : faces) {
        const Vec3f surface = addScaled(frame.translation, face.basis, -face.magnitude);
        Vec3f direction{99.0F, 98.0F, 97.0F};
        float scalar = -99.0F;
        check(cube.calcFaceGravity(surface, face.area, direction, scalar), "valid face accepted");
        check(nearVec(direction, face.basis), "valid face returns signed unit basis");
        check(near(scalar, 0.0F), "surface scalar clamps to zero");

        const Vec3f exterior = addScaled(surface, face.basis, -2.5F);
        check(cube.calcFaceGravity(exterior, face.area, direction, scalar), "exterior face accepted");
        check(near(scalar, 2.5F), "face distance subtracts basis magnitude");

        const Vec3f interior = addScaled(surface, face.basis, 4.0F);
        check(cube.calcFaceGravity(interior, face.area, direction, scalar), "interior face accepted");
        check(near(scalar, 0.0F), "interior face distance clamps");

        const Vec3f tangent = face.area == 10 || face.area == 16
            ? Vec3f{1.0F, 0.0F, 0.0F} : Vec3f{0.0F, 1.0F, 0.0F};
        const Vec3f tangential = addScaled(exterior, tangent, 13.0F);
        check(cube.calcFaceGravity(tangential, face.area, direction, scalar), "tangential face accepted");
        check(near(scalar, 2.5F), "tangential displacement does not affect face distance");
    }
}

void testInvalidFaceDoesNotWrite() {
    CubeGravity cube;
    for (const int area : {0, 13, 27, -1}) {
        Vec3f direction{11.0F, 12.0F, 13.0F};
        float scalar = 14.0F;
        check(!cube.calcFaceGravity({}, area, direction, scalar), "invalid face rejected");
        check(nearVec(direction, {11.0F, 12.0F, 13.0F}) && near(scalar, 14.0F),
              "invalid face leaves outputs untouched");
    }
}

void testNearZeroNormalizationAndZeroFaceBasis() {
    CubeGravityFrame frame{};
    frame.basis_x = {0.001F, 0.0F, 0.0F};
    frame.basis_y = {std::nextafter(0.001F, 1.0F), 0.0F, 0.0F};
    frame.basis_z = {};
    const CubeGravity cube(frame);

    Vec3f direction{9.0F, 8.0F, 7.0F};
    float scalar = -1.0F;
    check(cube.calcFaceGravity({}, 12, direction, scalar), "near-zero face accepted");
    check(nearVec(direction, {}), "component at +0.001 normalizes to zero direction");
    check(near(scalar, 0.0F), "near-zero face scalar remains zero");

    check(cube.calcFaceGravity({}, 10, direction, scalar), "above-threshold face accepted");
    check(nearVec(direction, {1.0F, 0.0F, 0.0F}), "component immediately above +0.001 is normalized");
    check(near(scalar, 0.0F), "above-threshold face scalar remains zero at origin");

    frame.basis_x = {-0.001F, 0.0F, 0.0F};
    const CubeGravity exact_negative(frame);
    check(exact_negative.calcFaceGravity({}, 12, direction, scalar), "negative threshold face accepted");
    check(nearVec(direction, {}), "component at -0.001 normalizes to zero direction");

    frame.basis_x = {std::nextafter(-0.001F, -1.0F), 0.0F, 0.0F};
    const CubeGravity below_negative_threshold(frame);
    check(below_negative_threshold.calcFaceGravity({}, 12, direction, scalar), "below-negative-threshold face accepted");
    check(nearVec(direction, {-1.0F, 0.0F, 0.0F}), "component immediately below -0.001 is normalized");

    check(cube.calcFaceGravity({0.0F, 0.0F, 2.0F}, 4, direction, scalar), "zero-basis face accepted");
    check(nearVec(direction, {}), "zero basis yields zero direction");
    check(near(scalar, 0.0F), "zero basis yields zero scalar");
}

} // namespace

int main() {
    testAll27Regions();
    testStrictBoundariesAndFlags();
    testRotatedTranslatedFrame();
    testScaledBasisAndLengths();
    testFaceGravity();
    testInvalidFaceDoesNotWrite();
    testNearZeroNormalizationAndZeroFaceBasis();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: CubeGravity native module (" << checks << " checks)\n";
    return EXIT_SUCCESS;
}
