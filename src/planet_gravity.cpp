#include "smg2/gravity/planet_gravity.hpp"

#include <cmath>

namespace smg2::gravity {
namespace {

constexpr float kMinimumGravityDistance = 1.0F;
constexpr float kGravityStrength = 4'000'000.0F;
constexpr float kNearZeroComponent = 0.001F;

Vec3f subtract(Vec3f lhs, Vec3f rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3f scale(Vec3f value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

float magnitude(Vec3f value) noexcept {
    const float squared = value.x * value.x + value.y * value.y + value.z * value.z;
    return std::sqrt(squared);
}

bool allComponentsNearZero(Vec3f value) noexcept {
    // The DOL's vector overload tests each component against +/-0.001 and
    // zeroes the output vector only when all three lie within those bounds.
    return value.x >= -kNearZeroComponent && value.x <= kNearZeroComponent &&
           value.y >= -kNearZeroComponent && value.y <= kNearZeroComponent &&
           value.z >= -kNearZeroComponent && value.z <= kNearZeroComponent;
}

Vec3f normalizedOrZero(Vec3f value, float length) noexcept {
    if (allComponentsNearZero(value)) return {};
    return scale(value, 1.0F / length);
}

} // namespace

PlanetGravity::PlanetGravity(PlanetGravityConfig config) noexcept : config_(config) {}

bool PlanetGravity::isInRangeSquare(float radius) const noexcept {
    if (config_.range < 0.0F) return true;
    const float boundary = config_.range + config_.distant;
    return radius < boundary * boundary;
}

bool PlanetGravity::isInRangeDistance(float radius) const noexcept {
    if (config_.range < 0.0F) return true;
    return radius < config_.range + config_.distant;
}

bool PlanetGravity::calcGravityFromMassPosition(
    Vec3f* pDirection, float* pDistance, Vec3f position,
    Vec3f massPosition) const noexcept {
    const Vec3f rawDirection = subtract(massPosition, position);
    const float distance = magnitude(rawDirection);
    if (!isInRangeDistance(distance)) return false;

    if (pDirection != nullptr) {
        *pDirection = normalizedOrZero(rawDirection, distance);
    }
    if (pDistance != nullptr) {
        *pDistance = distance;
    }
    return true;
}

bool PlanetGravity::calcGravity(Vec3f* pDestination, Vec3f position) const noexcept {
    if (pDestination == nullptr) return false;

    Vec3f direction{};
    float distance{};
    if (!calcOwnGravityVector(&direction, &distance, position)) return false;

    float adjustedDistance = distance - config_.distant;
    if (adjustedDistance < kMinimumGravityDistance) {
        adjustedDistance = kMinimumGravityDistance;
    }
    const float factor = kGravityStrength / (adjustedDistance * adjustedDistance);
    direction = scale(direction, factor);

    if (config_.inverse) direction = scale(direction, -1.0F);
    if (config_.valid_follower) direction = {};

    *pDestination = direction;
    return true;
}

bool PlanetGravity::calcOwnGravityVector(Vec3f*, float*, Vec3f) const noexcept {
    // The original base implementation at 0x80227330 always returns false.
    return false;
}

} // namespace smg2::gravity
