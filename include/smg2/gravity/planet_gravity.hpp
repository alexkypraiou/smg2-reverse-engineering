#pragma once

#include "smg2/gravity/cube_gravity.hpp"

namespace smg2::gravity {

struct PlanetGravityConfig {
    // A negative range means unbounded, matching PlanetGravity::isInRange*.
    float range{-1.0F};
    float distant{};
    bool inverse{};
    bool valid_follower{};
};

class PlanetGravity {
public:
    explicit PlanetGravity(PlanetGravityConfig config = {}) noexcept;
    virtual ~PlanetGravity() = default;

    [[nodiscard]] bool isInRangeSquare(float radius) const noexcept;
    [[nodiscard]] bool isInRangeDistance(float radius) const noexcept;

    // pDirection and pDistance are independently optional. On a range miss,
    // neither is written, matching the original output-pointer behavior.
    [[nodiscard]] bool calcGravityFromMassPosition(
        Vec3f* pDirection, float* pDistance, Vec3f position,
        Vec3f massPosition) const noexcept;

    // Returns false without writing pDestination if this gravity's own-vector
    // method declines the point. A null destination is not accepted here.
    [[nodiscard]] bool calcGravity(Vec3f* pDestination,
                                   Vec3f position) const noexcept;

    [[nodiscard]] const PlanetGravityConfig& config() const noexcept { return config_; }

protected:
    [[nodiscard]] virtual bool calcOwnGravityVector(
        Vec3f* pDirection, float* pDistance,
        Vec3f position) const noexcept;

private:
    PlanetGravityConfig config_;
};

} // namespace smg2::gravity
