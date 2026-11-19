#pragma once

#include <cstdint>

// 淡入淡出模式（与原项目 Internal/Core/FadeMode.cs 的 6 种一致）。
//
// 文档：docs/07-Animancer门面.md §4.2
namespace anim {

enum class FadeMode : uint8_t {
    FixedSpeed = 0,          // 0 -> 1 固定速度；非 0 起点会更快完成
    FixedDuration,           // 固定时长
    FromStart,               // 从 0 时间开始（必要时克隆 weightless 状态）
    NormalizedSpeed,         // 速度按动画长度归一化
    NormalizedDuration,      // 时长按动画长度归一化
    NormalizedFromStart,     // FromStart + NormalizedDuration
};

const char* toString(FadeMode mode) noexcept;

}  // namespace anim
