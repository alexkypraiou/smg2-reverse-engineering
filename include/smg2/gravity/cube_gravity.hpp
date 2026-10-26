#pragma once

#include <cstdint>

namespace smg2::gravity {

struct Vec3f {
    float x{};
    float y{};
    float z{};
};

struct CubeGravityFrame {
    Vec3f translation{};
    // The three vectors are the frame's basis vectors as stored by the game.
    Vec3f basis_x{1.0F, 0.0F, 0.0F};
    Vec3f basis_y{0.0F, 1.0F, 0.0F};
    Vec3f basis_z{0.0F, 0.0F, 1.0F};
    float half_length_x{1.0F};
    float half_length_y{1.0F};
    float half_length_z{1.0F};
    std::uint8_t active_faces{0x3FU};
};

class CubeGravity {
public:
    explicit CubeGravity(CubeGravityFrame frame = {}) noexcept;

    // Returns -1 when outside a disabled face, otherwise the game's 0..26 region code.
    [[nodiscard]] int calcGravityArea(Vec3f position) const noexcept;

    // Face area codes: -Z=4, -Y=10, -X=12, +X=14, +Y=16, +Z=22.
    // On invalid area, returns false and leaves both output references untouched.
    [[nodiscard]] bool calcFaceGravity(Vec3f position, int area,
                                       Vec3f& direction, float& scalar) const noexcept;

private:
    CubeGravityFrame frame_;
};

} // namespace smg2::gravity
