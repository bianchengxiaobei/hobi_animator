#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "anim/hobi/Transition.h"
#include "anim/integration/AnimationAssets.h"
#include "anim/integration/AnimationClipLibrary.h"

namespace anim {

class HobiAnimator;
class HobiState;

class AnimationTransitions {
public:
    AnimationTransitions() = default;
    ~AnimationTransitions() = default;

    AnimationTransitions(const AnimationTransitions&) = delete;
    AnimationTransitions& operator=(const AnimationTransitions&) = delete;
    AnimationTransitions(AnimationTransitions&&) noexcept = default;
    AnimationTransitions& operator=(AnimationTransitions&&) noexcept = default;

    bool build(const AnimationClipLibrary& library, const EventCallbackMap& callbacks,
               std::string& error);
    void clear() noexcept;

    int count() const noexcept { return static_cast<int>(_entries.size()); }
    bool empty() const noexcept { return _entries.empty(); }

    ClipTransition* find(std::string_view name) noexcept;
    const ClipTransition* find(std::string_view name) const noexcept;
    const ClipAssetInfo* info(std::string_view name) const noexcept;
    const ClipAssetInfo* info(int index) const noexcept;
    ClipTransition* transition(int index) noexcept;
    const ClipTransition* transition(int index) const noexcept;

    HobiState* play(HobiAnimator& animator, std::string_view name,
                         std::string* error = nullptr);
    bool bindRootBone(HobiAnimator& animator, std::string_view name,
                      std::string* error = nullptr) const;

private:
    struct Entry {
        ClipAssetInfo info;
        std::unique_ptr<ClipTransition> transition;
    };

    std::vector<Entry> _entries;
};

}

