#pragma once

#include "anim/skeleton/RootMotion.h"

namespace anim {

class IRootMotionTarget {
public:
    virtual ~IRootMotionTarget() = default;

    virtual void applyRootMotion(const RootMotionDelta& delta) = 0;
    virtual bool enabled() const { return true; }
};

class RootMotionRedirect {
public:
    IRootMotionTarget* target = nullptr;
    bool applyRootMotion = true;

    void update(const RootMotionDelta& delta) {
        if (!applyRootMotion || target == nullptr || !target->enabled()) return;
        target->applyRootMotion(delta);
    }
};

}  // namespace anim
