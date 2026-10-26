#pragma once

#include "smg2/gravity/planet_gravity.hpp"

namespace smg2::gravity {

// Portable affine transform represented by three basis columns and translation.
// It has no relationship to the Wii's in-memory matrix ABI.
struct AffineTransform3x4 {
    Vec3f basis_x{1.0F, 0.0F, 0.0F};
    Vec3f basis_y{0.0F, 1.0F, 0.0F};
    Vec3f basis_z{0.0F, 0.0F, 1.0F};
    Vec3f translation{};
};

class PointGravity final : public PlanetGravity {
public:
    explicit PointGravity(PlanetGravityConfig config = {},
                          Vec3f localPosition = {}) noexcept;

    void updateTransform(const AffineTransform3x4& transform) noexcept;
    [[nodiscard]] Vec3f worldPosition() const noexcept { return worldPosition_; }

protected:
    [[nodiscard]] bool calcOwnGravityVector(
        Vec3f* pDirection, float* pDistance,
        Vec3f position) const noexcept override;

private:
    Vec3f localPosition_{};
    Vec3f worldPosition_{};
};

} // namespace smg2::gravity
