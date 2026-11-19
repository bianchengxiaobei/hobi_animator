#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <limits>

// 数学类型统一封装（L1）。
//
// 文档：docs/02-核心数据类型.md §1、§8
// 说明：
//  - 只做纯代数，不硬编码左右手。手性转换由集成层负责（见 12）。
//  - glm 的四元数构造顺序是 (w, x, y, z)；与 Unity 的 (x, y, z, w) 序列化
//    顺序不同，跨边界时用 quatFromXYZW / quatToXYZW。

namespace anim {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Quat = glm::quat;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kEpsilon = 1.19209290e-7f;  // FLT_EPSILON
inline constexpr float kInfinity = std::numeric_limits<float>::infinity();

// glm 的 constexpr 支持有限，这些用 const（首次使用时初始化）。
inline const Quat kIdentityQuat{1.0f, 0.0f, 0.0f, 0.0f};  // (w,x,y,z)
inline const Vec3 kZeroVec3{0.0f, 0.0f, 0.0f};
inline const Vec3 kOneVec3{1.0f, 1.0f, 1.0f};

inline float radians(float degrees) noexcept { return glm::radians(degrees); }
inline float degrees(float radians) noexcept { return glm::degrees(radians); }

// Unity 的 m_LocalRotation 按 (x, y, z, w) 序列化。
inline Quat quatFromXYZW(float x, float y, float z, float w) noexcept {
    return Quat(w, x, y, z);
}
inline Vec4 quatToXYZW(const Quat& q) noexcept {
    return Vec4(q.x, q.y, q.z, q.w);
}

inline Quat safeNormalize(const Quat& q, const Quat& fallback = kIdentityQuat) noexcept {
    const float len2 = glm::dot(q, q);
    if (!(len2 > kEpsilon)) return fallback;
    return q * (1.0f / std::sqrt(len2));
}

inline bool isFinite(const Quat& q) noexcept {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
           std::isfinite(q.w);
}

}  // namespace anim
