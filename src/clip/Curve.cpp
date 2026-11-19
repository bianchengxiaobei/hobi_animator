#include "anim/clip/Curve.h"

#include <algorithm>
#include <cmath>

#include "anim/core/Assert.h"

namespace anim {
namespace {

// 三次 Hermite（非加权）。dt 为段时长，斜率单位为 dy/dx。
float hermite(float u, float dt, float v0, float outSlope0, float v1, float inSlope1) {
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h10 = u3 - 2.0f * u2 + u;
    const float h01 = -2.0f * u3 + 3.0f * u2;
    const float h11 = u3 - u2;
    return h00 * v0 + h10 * dt * outSlope0 + h01 * v1 + h11 * dt * inSlope1;
}

float bezier(float s, float p0, float p1, float p2, float p3) {
    const float u = 1.0f - s;
    return u * u * u * p0 + 3.0f * u * u * s * p1 + 3.0f * u * s * s * p2 +
           s * s * s * p3;
}

float bezierDerivative(float s, float p0, float p1, float p2, float p3) {
    const float u = 1.0f - s;
    return 3.0f * u * u * (p1 - p0) + 6.0f * u * s * (p2 - p1) +
           3.0f * s * s * (p3 - p2);
}

// 由 x 反解贝塞尔参数 s。先牛顿迭代，退化时二分兜底。
float solveBezierParameter(float target, float x0, float x1, float x2, float x3) {
    const float span = x3 - x0;
    float s = (std::abs(span) > kEpsilon) ? (target - x0) / span : 0.0f;
    s = std::clamp(s, 0.0f, 1.0f);

    for (int i = 0; i < 8; ++i) {
        const float error = bezier(s, x0, x1, x2, x3) - target;
        if (std::abs(error) < 1e-7f) return s;
        const float slope = bezierDerivative(s, x0, x1, x2, x3);
        if (std::abs(slope) < kEpsilon) break;
        const float next = s - error / slope;
        if (next < 0.0f || next > 1.0f) break;
        s = next;
    }

    float low = 0.0f;
    float high = 1.0f;
    for (int i = 0; i < 40; ++i) {
        const float mid = 0.5f * (low + high);
        if (bezier(mid, x0, x1, x2, x3) < target)
            low = mid;
        else
            high = mid;
    }
    return 0.5f * (low + high);
}

}  // namespace

float Curve::wrapTime(float time, float lo, float hi, WrapMode mode) noexcept {
    const float span = hi - lo;
    if (span <= 0.0f) return lo;

    switch (mode) {
        case WrapMode::Loop: {
            float t = std::fmod(time - lo, span);
            if (t < 0.0f) t += span;
            return lo + t;
        }
        case WrapMode::PingPong: {
            const float period = span * 2.0f;
            float t = std::fmod(time - lo, period);
            if (t < 0.0f) t += period;
            if (t > span) t = period - t;
            return lo + t;
        }
        case WrapMode::Once:
        case WrapMode::ClampForever:
        case WrapMode::Default:
        default:
            return std::clamp(time, lo, hi);
    }
}

bool Curve::isWeighted() const noexcept {
    for (const Keyframe& key : keys) {
        if (key.weightedMode != 0) return true;
    }
    return false;
}

float Curve::sampleSegment(std::size_t index, float time) const {
    ANIM_ASSERT(index + 1 < keys.size());
    const Keyframe& k0 = keys[index];
    const Keyframe& k1 = keys[index + 1];

    // Constant 段：保持左值直到下一关键帧。
    if (k0.constant) return k0.value;

    const float dt = k1.time - k0.time;
    if (!(dt > 0.0f)) return k0.value;

    if (k0.weightedMode != 0 || k1.weightedMode != 0) {
        // 斜率 + 权重 -> 贝塞尔控制点（时间/值空间）。
        const float w0 = std::clamp(k0.outWeight, 0.0f, 1.0f) * dt / 3.0f;
        const float w1 = std::clamp(k1.inWeight, 0.0f, 1.0f) * dt / 3.0f;

        const float x0 = k0.time;
        const float y0 = k0.value;
        const float x3 = k1.time;
        const float y3 = k1.value;

        const float x1 = x0 + w0;
        const float y1 = y0 + w0 * k0.outSlope;
        const float x2 = x3 - w1;
        const float y2 = y3 - w1 * k1.inSlope;

        const float s = solveBezierParameter(time, x0, x1, x2, x3);
        return bezier(s, y0, y1, y2, y3);
    }

    const float u = (time - k0.time) / dt;
    return hermite(u, dt, k0.value, k0.outSlope, k1.value, k1.inSlope);
}

float Curve::evaluate(float time) const {
    if (keys.empty()) return 0.0f;

    const float lo = keys.front().time;
    const float hi = keys.back().time;
    if (!(hi > lo)) return keys.front().value;

    float t = time;
    if (t < lo)
        t = wrapTime(time, lo, hi, preWrap);
    else if (t > hi)
        t = wrapTime(time, lo, hi, postWrap);

    return evaluateClamped(t);
}

float Curve::evaluateClamped(float time) const {
    if (keys.empty()) return 0.0f;

    const float lo = keys.front().time;
    const float hi = keys.back().time;
    if (!(hi > lo)) return keys.front().value;

    const float t = std::clamp(time, lo, hi);

    // 第一个 time > t 的关键帧。
    const auto begin = keys.begin();
    const auto end = keys.end();
    const auto it = std::upper_bound(
        begin, end, t,
        [](float value, const Keyframe& key) { return value < key.time; });
    const std::size_t upper = static_cast<std::size_t>(it - begin);

    if (upper == 0) return keys.front().value;
    if (upper >= keys.size()) return keys.back().value;
    return sampleSegment(upper - 1, t);
}

float Curve::evaluateUnclamped(float time) const {
    if (keys.empty()) return 0.0f;
    if (keys.size() == 1) return keys.front().value;

    const auto begin = keys.begin();
    const auto end = keys.end();
    const auto it = std::upper_bound(
        begin, end, time,
        [](float value, const Keyframe& key) { return value < key.time; });
    const std::size_t upper = static_cast<std::size_t>(it - begin);

    if (upper == 0) return sampleSegment(0, time);
    if (upper >= keys.size()) return sampleSegment(keys.size() - 2, time);
    return sampleSegment(upper - 1, time);
}

void Curve::sortByTime() {
    std::stable_sort(keys.begin(), keys.end(),
                     [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

}  // namespace anim
