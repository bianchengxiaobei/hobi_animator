#pragma once

#include <cstdint>

namespace anim {

class HobiState;
struct HobiEvent;

enum class EventKind : uint8_t {
    Normal = 0,
    End = 1,
};

class IEventObserver {
public:
    virtual ~IEventObserver() = default;
    virtual void onEventDispatched(const HobiState& state,
                                   const HobiEvent& event,
                                   EventKind kind,
                                   int eventIndex,
                                   bool playingForwards) = 0;
};

}
