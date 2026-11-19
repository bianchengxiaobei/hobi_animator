#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

#include "anim/clip/Clip.h"
#include "anim/clip/Curve.h"

namespace anim {

// 对应原项目 Utilities/Animation Jobs/AnimatedFloat.cs:52-64 的 Job 读取语义：
// 原项目通过 PropertyStreamHandle::GetFloat 读取动画流属性；本库把属性曲线
// 存在 PropertyChannel 中，按 path/attribute 绑定后采样。
namespace detail {

inline const Curve* animatedCurve(const Curve* curve, const PropertyChannel* property) {
    return property != nullptr ? &property->curve : curve;
}

}  // namespace detail

class AnimatedFloat {
public:
    const Curve* curve = nullptr;
    const PropertyChannel* property = nullptr;

    bool bindProperty(const Clip& clip, std::string_view path, std::string_view attribute) {
        property = clip.findProperty(path, attribute);
        return property != nullptr;
    }

    bool bindProperty(const Clip& clip, std::string_view attribute) {
        return bindProperty(clip, std::string_view{}, attribute);
    }

    const Curve* activeCurve() const noexcept { return detail::animatedCurve(curve, property); }
    bool hasCurve() const noexcept {
        const Curve* active = activeCurve();
        return active != nullptr && !active->empty();
    }
    float sample(float time) const { return hasCurve() ? activeCurve()->evaluate(time) : 0.0f; }
    explicit operator bool() const noexcept { return hasCurve(); }
};

class AnimatedBool {
public:
    const Curve* curve = nullptr;
    const PropertyChannel* property = nullptr;
    float threshold = 0.5f;

    bool bindProperty(const Clip& clip, std::string_view path, std::string_view attribute) {
        property = clip.findProperty(path, attribute);
        return property != nullptr;
    }

    bool bindProperty(const Clip& clip, std::string_view attribute) {
        return bindProperty(clip, std::string_view{}, attribute);
    }

    const Curve* activeCurve() const noexcept { return detail::animatedCurve(curve, property); }
    bool hasCurve() const noexcept {
        const Curve* active = activeCurve();
        return active != nullptr && !active->empty();
    }
    bool sample(float time) const {
        return hasCurve() && activeCurve()->evaluate(time) >= threshold;
    }
};

class AnimatedInt {
public:
    const Curve* curve = nullptr;
    const PropertyChannel* property = nullptr;

    bool bindProperty(const Clip& clip, std::string_view path, std::string_view attribute) {
        property = clip.findProperty(path, attribute);
        return property != nullptr;
    }

    bool bindProperty(const Clip& clip, std::string_view attribute) {
        return bindProperty(clip, std::string_view{}, attribute);
    }

    const Curve* activeCurve() const noexcept { return detail::animatedCurve(curve, property); }
    bool hasCurve() const noexcept {
        const Curve* active = activeCurve();
        return active != nullptr && !active->empty();
    }
    int sample(float time) const {
        if (!hasCurve()) return 0;
        return static_cast<int>(std::lround(activeCurve()->evaluate(time)));
    }
};

}  // namespace anim
