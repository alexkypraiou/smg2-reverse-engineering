#include "smg2/gravity/planet_gravity.hpp"
#include "smg2/gravity/point_gravity.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

using smg2::gravity::AffineTransform3x4;
using smg2::gravity::PlanetGravity;
using smg2::gravity::PlanetGravityConfig;
using smg2::gravity::PointGravity;
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

bool near(float actual, float expected, float epsilon = 1.0e-3F) {
    return std::fabs(actual - expected) <= epsilon;
}

bool nearVec(Vec3f actual, Vec3f expected, float epsilon = 1.0e-3F) {
    return near(actual.x, expected.x, epsilon) && near(actual.y, expected.y, epsilon) &&
           near(actual.z, expected.z, epsilon);
}

class FixedGravity final : public PlanetGravity {
public:
    FixedGravity(PlanetGravityConfig config, Vec3f direction, float distance,
                 bool available = true) noexcept
        : PlanetGravity(config), direction_(direction), distance_(distance), available_(available) {}

protected:
    bool calcOwnGravityVector(Vec3f* direction, float* distance,
                              Vec3f) const noexcept override {
        if (!available_) return false;
        if (direction != nullptr) *direction = direction_;
        if (distance != nullptr) *distance = distance_;
        return true;
    }

private:
    Vec3f direction_;
    float distance_;
    bool available_;
};

void testDistanceAndSquareRanges() {
    const PlanetGravity unbounded({-1.0F, -40.0F, false, false});
    check(unbounded.isInRangeDistance(1000.0F), "negative range is unbounded for distance");
    check(unbounded.isInRangeSquare(1.0e9F), "negative range is unbounded for square");

    const PlanetGravity bounded({3.0F, 1.0F, false, false});
    check(bounded.isInRangeDistance(3.999F), "distance threshold includes distant");
    check(!bounded.isInRangeDistance(4.0F), "distance boundary is strict");
    check(bounded.isInRangeSquare(15.999F), "square threshold squares range plus distant");
    check(!bounded.isInRangeSquare(16.0F), "square boundary is strict");

    const PlanetGravity negativeBoundary({2.0F, -3.0F, false, false});
    check(negativeBoundary.isInRangeSquare(0.5F), "square mode squares even a negative boundary");
    check(!negativeBoundary.isInRangeDistance(0.0F), "distance mode preserves a negative boundary");
}

void testGravityFromMassPosition() {
    const PlanetGravity bounded({5.0F, 1.0F, false, false});
    Vec3f direction{99.0F, 99.0F, 99.0F};
    float distance = -1.0F;
    check(bounded.calcGravityFromMassPosition(&direction, &distance,
                                              {1.0F, 2.0F, 3.0F},
                                              {4.0F, 6.0F, 3.0F}),
          "mass position inside range is accepted");
    check(nearVec(direction, {0.6F, 0.8F, 0.0F}), "direction points from position to mass");
    check(near(distance, 5.0F), "distance output preserves the original magnitude");

    direction = {11.0F, 12.0F, 13.0F};
    distance = 14.0F;
    check(!bounded.calcGravityFromMassPosition(&direction, &distance,
                                               {}, {6.0F, 0.0F, 0.0F}),
          "mass position at exact range boundary is rejected");
    check(nearVec(direction, {11.0F, 12.0F, 13.0F}) && near(distance, 14.0F),
          "range miss leaves both optional outputs untouched");

    check(bounded.calcGravityFromMassPosition(nullptr, &distance, {}, {3.0F, 4.0F, 0.0F}),
          "direction output may be null");
    check(near(distance, 5.0F), "distance output works without a direction output");
    check(bounded.calcGravityFromMassPosition(&direction, nullptr, {}, {3.0F, 4.0F, 0.0F}),
          "distance output may be null");
    check(nearVec(direction, {0.6F, 0.8F, 0.0F}), "direction output works without a distance output");

    const PlanetGravity nearZero({-1.0F, 0.0F, false, false});
    distance = -1.0F;
    check(nearZero.calcGravityFromMassPosition(&direction, &distance,
                                                {}, {0.001F, 0.0005F, -0.001F}),
          "near-zero mass vector remains a valid in-range result");
    check(nearVec(direction, {}), "vector overload zeroes components within inclusive epsilon");
    check(distance > 0.0F, "near-zero vector still reports its magnitude");

    check(nearZero.calcGravityFromMassPosition(&direction, &distance, {}, {0.002F, 0.0F, 0.0F}),
          "vector outside component epsilon is normalized");
    check(nearVec(direction, {1.0F, 0.0F, 0.0F}), "nonzero vector direction normalized");
}

void testBaseDeclinesAndCommonGravityScaling() {
    const PlanetGravity base;
    Vec3f output{7.0F, 8.0F, 9.0F};
    check(!base.calcGravity(&output, {}), "base own-vector routine declines gravity");
    check(nearVec(output, {7.0F, 8.0F, 9.0F}), "declined gravity leaves caller output alone");
    check(!base.calcGravity(nullptr, {}), "null final output is rejected safely");

    const FixedGravity normal({-1.0F, 1.0F, false, false}, {0.0F, 1.0F, 0.0F}, 5.0F);
    check(normal.calcGravity(&output, {}), "derived own-vector result is accepted");
    check(nearVec(output, {0.0F, 250000.0F, 0.0F}),
          "gravity uses 4,000,000 divided by adjusted distance squared");

    const FixedGravity clamp({-1.0F, 0.0F, false, false}, {1.0F, 0.0F, 0.0F}, 0.25F);
    check(clamp.calcGravity(&output, {}), "sub-unit radius remains valid");
    check(nearVec(output, {4'000'000.0F, 0.0F, 0.0F}), "effective distance clamps to one");

    const FixedGravity inverse({-1.0F, 0.0F, true, false}, {0.0F, -1.0F, 0.0F}, 2.0F);
    check(inverse.calcGravity(&output, {}), "inverse gravity is calculated");
    check(nearVec(output, {0.0F, 1'000'000.0F, 0.0F}), "inverse flag negates gravity");

    const FixedGravity follower({-1.0F, 0.0F, true, true}, {2.0F, 3.0F, 4.0F}, 2.0F);
    check(follower.calcGravity(&output, {}), "valid follower gravity reports success");
    check(nearVec(output, {}), "valid follower zeros gravity after inversion");

    const FixedGravity declined({-1.0F, 0.0F, false, false}, {1.0F, 0.0F, 0.0F}, 1.0F, false);
    output = {21.0F, 22.0F, 23.0F};
    check(!declined.calcGravity(&output, {}), "derived type can decline an out-of-range vector");
    check(nearVec(output, {21.0F, 22.0F, 23.0F}), "declined derived vector causes no caller write");
}

void testPointGravityTransformAndVector() {
    PointGravity point({6.0F, 1.0F, false, false}, {2.0F, 3.0F, 4.0F});
    AffineTransform3x4 transform;
    transform.basis_x = {0.0F, 1.0F, 0.0F};
    transform.basis_y = {-1.0F, 0.0F, 0.0F};
    transform.basis_z = {0.0F, 0.0F, 1.0F};
    transform.translation = {10.0F, 20.0F, 30.0F};
    point.updateTransform(transform);
    check(nearVec(point.worldPosition(), {7.0F, 22.0F, 34.0F}),
          "PointGravity applies rotation and translation to its local point");

    Vec3f gravity{};
    check(point.calcGravity(&gravity, {10.0F, 26.0F, 34.0F}),
          "point gravity accepts a position within inherited range");
    check(nearVec(gravity, {-150000.0F, -200000.0F, 0.0F}),
          "point gravity points at its world-space position and uses distant falloff");

    PointGravity inverse({6.0F, 1.0F, true, false}, point.worldPosition());
    check(inverse.calcGravity(&gravity, {10.0F, 26.0F, 34.0F}),
          "inverse point gravity is accepted");
    check(nearVec(gravity, {150000.0F, 200000.0F, 0.0F}),
          "point gravity inversion flips the final vector");

    PointGravity boundary({5.0F, 0.0F, false, false}, {});
    gravity = {31.0F, 32.0F, 33.0F};
    check(!boundary.calcGravity(&gravity, {3.0F, 4.0F, 0.0F}),
          "PointGravity rejects the strict inherited range boundary");
    check(nearVec(gravity, {31.0F, 32.0F, 33.0F}),
          "out-of-range PointGravity does not write the final vector");
}

void testPointScalarEpsilonDiffersFromVectorOverload() {
    PointGravity point;
    Vec3f gravity{};

    check(point.calcGravity(&gravity, {0.0008F, 0.0008F, 0.0F}),
          "short PointGravity vector is accepted");
    constexpr float diagonal = 2'828'427.0F;
    check(nearVec(gravity, {-diagonal, -diagonal, 0.0F}, 4.0F),
          "PointGravity tests magnitude epsilon and normalizes the short diagonal");

    check(point.calcGravity(&gravity, {0.0009F, 0.0F, 0.0F}),
          "sub-epsilon PointGravity vector is accepted");
    check(nearVec(gravity, {}), "PointGravity zeroes a magnitude strictly below 0.001");

    check(point.calcGravity(&gravity, {0.001F, 0.0F, 0.0F}),
          "exact PointGravity epsilon is accepted");
    check(nearVec(gravity, {-4'000'000.0F, 0.0F, 0.0F}, 2.0F),
          "scalar epsilon check is strict at exactly 0.001");
}

} // namespace

int main() {
    testDistanceAndSquareRanges();
    testGravityFromMassPosition();
    testBaseDeclinesAndCommonGravityScaling();
    testPointGravityTransformAndVector();
    testPointScalarEpsilonDiffersFromVectorOverload();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: PlanetGravity and PointGravity native modules (" << checks << " checks)\n";
    return EXIT_SUCCESS;
}
