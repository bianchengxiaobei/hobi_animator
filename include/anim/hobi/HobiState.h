#pragma once

#include <cstdint>
#include <memory>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/hobi/HobiNode.h"
#include "anim/hobi/Event.h"
#include "anim/hobi/Key.h"

// 动画状态（对应 HobiState）。
//
// 文档：docs/07-Hobi门面.md §3
// 对齐 Internal/Core/AnimancerState.cs：
//  - TimeD 延迟设置（post-update 中改时间延迟到下一次 pre-update，:472-526）
//  - IsPlaying 脏标记（:262,315-328,803-822）
//  - Play/Stop 清事件（:357-383）
//  - Duration/RemainingDuration/NormalizedEndTime（:666-777）
namespace anim {

class HobiLayer;
class HobiPlayable;
struct Clip;
class EventDispatcher;

class HobiState : public HobiNode, public IAnimationClipCollection {
public:
    explicit HobiState(HobiPlayable* root = nullptr);
    ~HobiState() override;

    HobiLayer* layer() const noexcept override;

    virtual const Clip* clip() const noexcept { return nullptr; }
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

    Key key() const noexcept { return _key; }
    void setKey(Key key) noexcept { _key = key; }

    // ---- 播放 ----
    bool isPlaying() const noexcept { return _isPlaying; }
    void setIsPlaying(bool value);

    bool isActive() const noexcept { return _isPlaying && targetWeight() > 0.0f; }
    bool isStopped() const noexcept { return !_isPlaying && weight() == 0.0f; }

    void play();
    void stop() override;

    // ---- 时间 ----
    // RawTime（:472-526）：ManualMixerState 会重写为子状态的加权平均。
    virtual double timeD() const noexcept { return _time; }
    virtual void setTimeD(double value);

    float time() const noexcept { return static_cast<float>(timeD()); }
    void setTime(float value) { setTimeD(value); }

    double normalizedTimeD() const;
    void setNormalizedTimeD(double value);

    float normalizedTime() const { return static_cast<float>(normalizedTimeD()); }
    void setNormalizedTime(float value) { setNormalizedTimeD(value); }

    virtual void moveTime(double time, bool normalized);

    // ---- 时长 ----
    virtual float length() const { return 0.0f; }
    virtual bool isLooping() const noexcept { return false; }

    float normalizedEndTime() const;
    void setNormalizedEndTime(float value);

    float duration() const;
    void setDuration(float value);

    float remainingDuration() const;
    void setRemainingDuration(float value);

    // ---- 事件 ----
    EventSequence* events() noexcept { return _events; }
    const EventSequence* events() const noexcept { return _events; }
    void setEvents(EventSequence* events);
    bool hasEvents() const noexcept { return _events != nullptr && !_events->isEmpty(); }
    void clearEvents();

    bool automaticallyClearEvents() const noexcept { return _automaticallyClearEvents; }
    void setAutomaticallyClearEvents(bool value) noexcept { _automaticallyClearEvents = value; }

    EventDispatcher* dispatcher() const noexcept { return _dispatcher.get(); }

    // ---- 更新/销毁 ----
    void update(bool& needsMoreUpdates) override;
    bool requiresContinuousUpdate() const noexcept override { return _isPlaying; }
    virtual void destroy();
    virtual HobiState* clone(HobiPlayable* root) = 0;

protected:
    friend class HobiLayer;

    void onStartFade() override;
    // IsPlaying 变化后的回调（ManualMixerState 用它同步所有子状态）。
    virtual void onSetIsPlaying() {}
    void setTimeOnly(double value);
    void applyTimeToPlayable();
    EventSequence& ensureEvents();

    double _time = 0.0;
    bool _mustSetTime = false;
    uint64_t _lastTimeUpdateFrame = ~0ull;

    Key _key{};

    bool _isPlaying = false;
    bool _isPlayingDirty = true;

    std::unique_ptr<EventSequence> _ownedEvents;
    std::unique_ptr<EventDispatcher> _dispatcher;
    EventSequence* _events = nullptr;
    bool _automaticallyClearEvents = true;
};

}  // namespace anim
