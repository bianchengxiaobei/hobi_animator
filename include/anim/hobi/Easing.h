#pragma once

#include <cmath>

// 缓动函数（easings.net 常用子集）。
//
// 文档：docs/07-Hobi门面.md §8
// 全部接受 t ∈ [0,1] 并返回缓动后的进度。
namespace anim {
namespace easing {

inline float linear(float t) noexcept { return t; }

inline float inQuad(float t) noexcept { return t * t; }
inline float outQuad(float t) noexcept { const float u = 1.0f - t; return 1.0f - u * u; }
inline float inOutQuad(float t) noexcept {
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

inline float inCubic(float t) noexcept { return t * t * t; }
inline float outCubic(float t) noexcept { const float u = 1.0f - t; return 1.0f - u * u * u; }
inline float inOutCubic(float t) noexcept {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);
}

inline float inSine(float t) noexcept { return 1.0f - std::cos(t * 1.57079632679f); }
inline float outSine(float t) noexcept { return std::sin(t * 1.57079632679f); }
inline float inOutSine(float t) noexcept { return 0.5f - 0.5f * std::cos(t * 3.14159265359f); }

inline float inExpo(float t) noexcept { return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f); }
inline float outExpo(float t) noexcept { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
inline float inOutExpo(float t) noexcept {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t < 0.5f ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
                    : 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
}

inline float inBack(float t) noexcept {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}
inline float outBack(float t) noexcept {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
inline float inOutBack(float t) noexcept {
    constexpr float c1 = 1.70158f;
    constexpr float c2 = c1 * 1.525f;
    const float u = t * 2.0f;
    if (u < 1.0f) return 0.5f * (u * u * ((c2 + 1.0f) * u - c2));
    const float v = u - 2.0f;
    return 0.5f * (v * v * ((c2 + 1.0f) * v + c2) + 2.0f);
}

}  // namespace easing
}  // namespace anim
