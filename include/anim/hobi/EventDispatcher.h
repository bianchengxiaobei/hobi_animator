#pragma once

#include <cstdint>

#include "anim/hobi/Event.h"
#include "anim/hobi/EventObserver.h"
#include "anim/core/IUpdatable.h"

// 事件派发器（对应 HobiState.EventDispatcher）。
//
// 文档：docs/07-Hobi门面.md §9.2
// 对齐 Internal/Core/AnimancerState.EventDispatcher.cs：
//  - 在 post-update 运行（事件里切换动画安全）
//  - 前向/后向、循环/非循环、零长度
//  - End 事件在越过结束点后每帧触发
namespace anim {

class HobiState;

class EventDispatcher final : public IUpdatable {
public:
    explicit EventDispatcher(HobiState& state);

    void setEvents(EventSequence* events);
    EventSequence* events() const noexcept { return _events; }

    void setObserver(IEventObserver* observer) noexcept { _observer = observer; }
    IEventObserver* observer() const noexcept { return _observer; }

    void onTimeChanged();
    void release();
    bool released() const noexcept { return _released; }

    void update(bool& needsMoreUpdates) override;

private:
    static constexpr int kRecalculateIndex = INT32_MIN;

    void notify(const HobiEvent& event, EventKind kind, int eventIndex,
                bool playingForwards) const;
    void updateZeroLength();
    void checkGeneralEvents(float currentTime);
    void validateNextEventIndex(float& currentTime, float& playDirectionFloat,
                                int& playDirectionInt);

    static int getLoopDelta(float previousTime, float nextTime, float eventTime);
    bool invokeAllEvents(int count, int playDirectionInt);
    bool nextEvent(int playDirectionInt);
    bool nextEventLooped(int playDirectionInt);

    HobiState* _state = nullptr;
    EventSequence* _events = nullptr;
    IEventObserver* _observer = nullptr;

    float _previousTime = 0.0f;
    int _nextEventIndex = kRecalculateIndex;
    int _sequenceVersion = -1;
    bool _wasPlayingForwards = true;
    bool _isLooping = false;
    bool _released = false;
};

}  // namespace anim
