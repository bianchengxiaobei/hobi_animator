#pragma once

#include <cstddef>

#include "anim/core/Math.h"

// 局部 TRS。对应 Unity 的 m_LocalPosition / m_LocalRotation / m_LocalScale
// （见 Examples/Art/Spider Bot/SpiderBot-MoveUp.anim 的 m_Local* 通道）。
//
// 文档：docs/02-核心数据类型.md §2
namespace anim {

struct alignas(16) Transform {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    float _padding0 = 0.0f;
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // (w,x,y,z)
    Vec3 scale{1.0f, 1.0f, 1.0f};
    float _padding1 = 0.0f;

    Transform() = default;
    explicit Transform(const Vec3& translation) : translation(translation) {}
    Transform(const Vec3& translation, const Quat& rotation)
        : translation(translation), rotation(rotation) {}
    Transform(const Vec3& translation, const Quat& rotation, const Vec3& scale)
        : translation(translation), rotation(rotation), scale(scale) {}

    // T * R * S
    Mat4 toMatrix() const;

    // 仅支持带统一/非统一缩放的仿射矩阵；含剪切时结果不保证。
    static Transform fromMatrix(const Mat4& matrix);

    // 组合：child 的 local TRS 放到 parent 之下。
    // translation = parentT + parentR * (parentS * localT)
    // rotation    = parentR * localR
    // scale       = parentS * localS
    static Transform compose(const Transform& parent, const Transform& local);

    static const Transform& identity() noexcept;
};

static_assert(sizeof(Transform) == 48);
static_assert(offsetof(Transform, translation) == 0);
static_assert(offsetof(Transform, rotation) == 16);
static_assert(offsetof(Transform, scale) == 32);

inline bool operator==(const Transform& a, const Transform& b) noexcept {
    return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
inline bool operator!=(const Transform& a, const Transform& b) noexcept { return !(a == b); }

}  // namespace anim
