#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "anim/core/Hash.h"

// 事件与事件序列（HobiEvent / EventSequence）。
//
// 文档：docs/07-Hobi门面.md §9.1
// 语义要点（与原项目一致）：
//  - count() 只统计普通事件；End 事件单独存放（normalizedTime 默认 NaN）。
//  - 普通事件按 normalizedTime 升序插入。
//  - Version 只在普通事件修改时自增（End 事件修改不增加）。
namespace anim {

struct HobiEvent {
    float normalizedTime = 0.0f;
    std::string name;
    std::function<void()> callback;

    HobiEvent() = default;
    HobiEvent(float time, std::function<void()> cb, std::string eventName = {})
        : normalizedTime(time), name(std::move(eventName)), callback(std::move(cb)) {}

    void invoke() const {
        if (callback) callback();
    }

    bool hasCallback() const noexcept { return static_cast<bool>(callback); }
};

class EventSequence {
public:
    static constexpr int kDefaultCapacity = 8;

    EventSequence() = default;
    EventSequence(const EventSequence&) = default;
    EventSequence& operator=(const EventSequence&) = default;

    int count() const noexcept { return static_cast<int>(_events.size()); }
    int version() const noexcept { return _version; }

    // 普通事件（不含 End）。
    HobiEvent& at(int index);
    const HobiEvent& at(int index) const;
    HobiEvent& operator[](int index) { return at(index); }
    const HobiEvent& operator[](int index) const { return at(index); }

    // End 事件（OnEnd）。
    HobiEvent& endEvent() noexcept { return _endEvent; }
    const HobiEvent& endEvent() const noexcept { return _endEvent; }

    // 未显式设置时返回 NaN，由调用方按速度方向取默认值。
    float normalizedEndTime() const noexcept { return _endEvent.normalizedTime; }
    void setNormalizedEndTime(float value) noexcept { _endEvent.normalizedTime = value; }

    bool isEmpty() const noexcept {
        return !_endEvent.hasCallback() && std::isnan(_endEvent.normalizedTime) &&
               _events.empty();
    }

    // 按时间有序插入；返回插入下标。
    int add(HobiEvent event);
    int add(float normalizedTime, std::function<void()> callback, std::string name = {});

    void addCallback(int index, std::function<void()> callback);
    void remove(int index);
    void clear();

    int indexOf(std::string_view name) const;
    int indexOfRequired(std::string_view name) const;
    HobiEvent& byName(std::string_view name) { return at(indexOfRequired(name)); }

    void copyFrom(const EventSequence& other);

    // 速度 < 0 时默认结束点为 0，否则为 1。
    static float defaultNormalizedStartTime(float speed) noexcept { return speed < 0.0f ? 1.0f : 0.0f; }
    static float defaultNormalizedEndTime(float speed) noexcept { return speed < 0.0f ? 0.0f : 1.0f; }

private:
    void markModified() noexcept { ++_version; }

    std::vector<HobiEvent> _events;
    HobiEvent _endEvent{std::numeric_limits<float>::quiet_NaN(), nullptr};
    int _version = 0;
};

}  // namespace anim
