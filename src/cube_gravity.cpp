#include "smg2/gravity/cube_gravity.hpp"

#include <cmath>

namespace smg2::gravity {
namespace {

constexpr std::uint8_t kNegativeX = 2U;
constexpr std::uint8_t kPositiveX = 1U;
constexpr std::uint8_t kNegativeY = 8U;
constexpr std::uint8_t kPositiveY = 4U;
constexpr std::uint8_t kNegativeZ = 32U;
constexpr std::uint8_t kPositiveZ = 16U;

Vec3f subtract(Vec3f lhs, Vec3f rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3f negate(Vec3f value) noexcept {
    return {-value.x, -value.y, -value.z};
}

float dot(Vec3f lhs, Vec3f rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

float length(Vec3f value) noexcept {
    return std::sqrt(dot(value, value));
}

Vec3f normalizedLikeReference(Vec3f value, float magnitude) noexcept {
    // MathUtil::isNearZero checks each component independently against +/-0.001f.
    if (value.x >= -0.001F && value.x <= 0.001F &&
        value.y >= -0.001F && value.y <= 0.001F &&
        value.z >= -0.001F && value.z <= 0.001F) {
        return {};
    }
    return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

int axisRegion(float coordinate, float half_length,
               std::uint8_t active_faces, std::uint8_t negative_bit,
               std::uint8_t positive_bit, int between_code, int positive_code) noexcept {
    if (coordinate < -half_length) {
        return (active_faces & negative_bit) != 0U ? 0 : -1;
    }
    if (coordinate <= half_length) {
        return between_code;
    }
    return (active_faces & positive_bit) != 0U ? positive_code : -1;
}

} // namespace

CubeGravity::CubeGravity(CubeGravityFrame frame) noexcept : frame_(frame) {}

int CubeGravity::calcGravityArea(Vec3f position) const noexcept {
    const Vec3f relative = subtract(position, frame_.translation);

    // This preserves the source's order literally: dot with each basis, divide by
    // mLen*, then compare the result against +/-mLen*.
    const float x = dot(relative, frame_.basis_x) / frame_.half_length_x;
    const float y = dot(relative, frame_.basis_y) / frame_.half_length_y;
    const float z = dot(relative, frame_.basis_z) / frame_.half_length_z;

    const int x_region = axisRegion(x, frame_.half_length_x, frame_.active_faces,
                                    kNegativeX, kPositiveX, 1, 2);
    if (x_region < 0) return -1;
    const int y_region = axisRegion(y, frame_.half_length_y, frame_.active_faces,
                                    kNegativeY, kPositiveY, 3, 6);
    if (y_region < 0) return -1;
    const int z_region = axisRegion(z, frame_.half_length_z, frame_.active_faces,
                                    kNegativeZ, kPositiveZ, 9, 18);
    if (z_region < 0) return -1;
    return x_region + y_region + z_region;
}

bool CubeGravity::calcFaceGravity(Vec3f position, int area,
                                  Vec3f& direction, float& scalar) const noexcept {
    Vec3f face_vector{};
    switch (area) {
    case 4: face_vector = frame_.basis_z; break;
    case 10: face_vector = frame_.basis_y; break;
    case 12: face_vector = frame_.basis_x; break;
    case 14: face_vector = negate(frame_.basis_x); break;
    case 16: face_vector = negate(frame_.basis_y); break;
    case 22: face_vector = negate(frame_.basis_z); break;
    default: return false;
    }

    const float magnitude = length(face_vector);
    const Vec3f face_direction = normalizedLikeReference(face_vector, magnitude);
    const float height = dot(face_direction, subtract(frame_.translation, position)) - magnitude;

    direction = face_direction;
    scalar = height < 0.0F ? 0.0F : height;
    return true;
}

} // namespace smg2::gravity
