#pragma once
#include <numbers>
#include <cmath>

namespace OptiMath
{
// Radians per degree. Used to convert from degrees to radians
inline constexpr float DegToRad = std::numbers::pi_v<float> / 180.0f;
// Degrees per radian. Used to convert from radians to degrees
inline constexpr float RadToDeg = 180.0f / std::numbers::pi_v<float>;

// Converts from an angle in degrees to radians
[[nodiscard]] inline constexpr float GetRadiansFromDeg(const float deg) { return deg * DegToRad; }

// Converts from an angle in radians to degrees
[[nodiscard]] inline constexpr float GetDegreesFromRad(const float rad) { return rad * RadToDeg; }

/**
 * @brief Calculates the vertical field of view for a camera from its horizontal FOV and its
 * viewport dimensions according to the formula: vFov = 2 * arctan( tan( hFov / 2 ) * ( h / w ) ).
 * @param hFovRad: Horizontal field of view
 * @param width: Width of the camera viewport
 * @param height: Height of the camera viewport
 * @return Vertical FOV in radians
 */
[[nodiscard]] inline float GetVerticalFovFromHorizontal(const float hFovRad, const float width, const float height)
{
    if (width <= 0.0f)
        return 0.0f;
    return 2.0f * std::atan(std::tan(hFovRad * 0.5f) * (height / width));
}

/**
 * @brief Calculates the horizontal field of view for a camera from its vertical FOV and its
 * viewport dimensions according to the formula: hFov = 2 * arctan( tan( vFov / 2 ) * ( w / h ) ).
 * @param vFovRad: Vertical field of view in radians
 * @param width: Width of the camera viewport
 * @param height: Height of the camera viewport
 * @return Horizontal FOV in radians
 */
[[nodiscard]] inline float GetHorizontalFovFromVertical(const float vFovRad, const float width, const float height)
{
    if (height <= 0.0f)
        return 0.0f;
    return 2.0f * std::atan(std::tan(vFovRad * 0.5f) * (width / height));
}
} // namespace OptiMath
