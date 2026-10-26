#include "smg2/gravity/point_gravity.hpp"

#include <cmath>

namespace smg2::gravity {
namespace {

constexpr float kNearZeroMagnitude = 0.001F;

Vec3f subtract(Vec3f lhs, Vec3f rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

float magnitude(Vec3f value) noexcept {
    const float squared = value.x * value.x + value.y * value.y + value.z * value.z;
    return std::sqrt(squared);
}

Vec3f normalizedPointDirection(Vec3f value, float length) noexcept {
    // PointGravity calls the scalar isNearZero overload, so its threshold is
    // on vector magnitude and is strict (< 0.001), unlike the vector overload.
    if (std::fabs(length) < kNearZeroMagnitude) return {};
    const float factor = 1.0F / length;
    return {value.x * factor, value.y * factor, value.z * factor};
}

} // namespace

PointGravity::PointGravity(PlanetGravityConfig config, Vec3f localPosition) noexcept
    : PlanetGravity(config), localPosition_(localPosition), worldPosition_(localPosition) {}

void PointGravity::updateTransform(const AffineTransform3x4& transform) noexcept {
    const Vec3f& p = localPosition_;
    const Vec3f& x = transform.basis_x;
    const Vec3f& y = transform.basis_y;
    const Vec3f& z = transform.basis_z;
    worldPosition_ = {
        transform.translation.x + x.x * p.x + y.x * p.y + z.x * p.z,
        transform.translation.y + x.y * p.x + y.y * p.y + z.y * p.z,
        transform.translation.z + x.z * p.x + y.z * p.y + z.z * p.z,
    };
}

bool PointGravity::calcOwnGravityVector(
    Vec3f* pDirection, float* pDistance, Vec3f position) const noexcept {
    const Vec3f rawDirection = subtract(worldPosition_, position);
    const float distance = magnitude(rawDirection);
    if (!isInRangeDistance(distance)) return false;

    if (pDirection != nullptr) {
        *pDirection = normalizedPointDirection(rawDirection, distance);
    }
    if (pDistance != nullptr) {
        *pDistance = distance;
    }
    return true;
}

} // namespace smg2::gravity
