#pragma once

#include <functional>
#include <vector>

#include "anim/core/IUpdatable.h"

// 自定义淡入淡出（对应 CustomFade / CustomFade.Delegate / CustomFade.Curve）。
//
// 文档：docs/07-Hobi门面.md §8
// 对齐 Utilities/Custom Fade/CustomFade.cs：
//  - Apply(HobiState) 同时接管同层其它正在淡出的状态（:84-100）
//  - Update 每帧按曲线设置目标/淡出权重（:137-186）
//  - 播放其它状态或层命令变化时自动取消（:141-151）
namespace anim {

class HobiLayer;
class HobiNode;
class HobiPlayable;
class HobiState;

class CustomFade : public IUpdatable {
public:
    using Curve = std::function<float(float)>;

    // 对节点当前的淡入淡出发起自定义曲线（node 必须正在淡入/淡出）。
    static void apply(HobiNode& node, Curve curve);

    // 对状态发起自定义淡入，并接管同层其它正在淡出的状态。
    static void apply(HobiState& state, Curve curve);

    // 对层 0 的当前状态发起自定义淡入。
    static void apply(HobiPlayable& playable, Curve curve);

    void update(bool& needsMoreUpdates) override;

    bool requiresContinuousUpdate() const noexcept override { return false; }

private:
    struct NodeWeight {
        HobiNode* node = nullptr;
        float startingWeight = 0.0f;
    };

    explicit CustomFade(Curve curve);

    void applyNode(HobiNode& node);
    float calculateWeight(float progress) const;
    void cancel();
    static void finishNode(HobiNode& node);

    Curve _curve;
    float _time = 0.0f;
    float _fadeSpeed = 0.0f;
    NodeWeight _target;
    HobiLayer* _layer = nullptr;
    int _commandCount = 0;
    std::vector<NodeWeight> _fadeOutNodes;
};

}  // namespace anim
