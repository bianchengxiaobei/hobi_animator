#pragma once

#include <cstdint>
#include <vector>

#include "anim/core/Math.h"

// 曲线（Unity AnimationCurve 的等价实现）。
//
// 数据来源：.anim 中的 m_Curve 关键帧，字段为
//   time, value, inSlope, outSlope, tangentMode, weightedMode, inWeight, outWeight
// （见 Examples/Art/Humanoid Animations/Humanoid-Walk.anim:21-31）
//
// 文档：docs/03-动画数据与曲线.md §1
//
// 关键实现约定（已用示例 .anim 实测校准，见 §4）：
//  - 相邻关键帧之间是三次 Hermite；inSlope/outSlope 是 dy/dx。
//  - weightedMode != 0 时，把斜率+权重转成贝塞尔控制点后求值（见 evaluate）。
//  - constant == true 的段（关键帧到下一关键帧）直接保持左值（Unity Constant 切线）。
//    该标志由转换器依据 Unity 的 tangentMode 位域精确解码后设置——运行时不猜位布局。
//  - m_PreInfinity / m_PostInfinity 存为 WrapMode（示例数据均为 Loop=2）。
namespace anim {

enum class WrapMode : uint8_t {
    Default = 0,
    Once = 1,
    Loop = 2,
    PingPong = 4,
    ClampForever = 8,
};

struct Keyframe {
    float time = 0.0f;
    float value = 0.0f;
    float inSlope = 0.0f;   // 左切线（斜率）
    float outSlope = 0.0f;  // 右切线（斜率）
    float inWeight = 1.0f / 3.0f;
    float outWeight = 1.0f / 3.0f;
    uint8_t tangentMode = 0;
    uint8_t weightedMode = 0;
    bool constant = false;  // 本关键帧 -> 下一关键帧为常量段
};

class Curve {
public:
    std::vector<Keyframe> keys;

    WrapMode preWrap = WrapMode::ClampForever;
    WrapMode postWrap = WrapMode::ClampForever;

    bool empty() const noexcept { return keys.empty(); }
    float length() const noexcept { return keys.empty() ? 0.0f : keys.back().time; }

    // 应用 preWrap/postWrap 后求值；范围外按 wrap 模式处理。
    float evaluate(float time) const;

    // 不应用 wrap，直接把 time 夹到 [first.time, last.time]。
    float evaluateClamped(float time) const;

    // 不应用 wrap 也不夹紧：范围外按最近一段 Hermite 的参数延拓。
    float evaluateUnclamped(float time) const;

    // 该曲线是否使用加权切线（任一端点开启 weightedMode）。
    bool isWeighted() const noexcept;

    // 按 time 升序排序（稳定性无关；只在关键帧时间互异时安全）。
    void sortByTime();

private:
    float sampleSegment(std::size_t index, float time) const;
    static float wrapTime(float time, float lo, float hi, WrapMode mode) noexcept;
};

}  // namespace anim
