#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "anim/clip/Clip.h"
#include "anim/integration/AnimationAssets.h"

namespace anim {

class AnimationClipLibrary {
public:
    AnimationClipLibrary() = default;
    ~AnimationClipLibrary() = default;

    AnimationClipLibrary(const AnimationClipLibrary&) = delete;
    AnimationClipLibrary& operator=(const AnimationClipLibrary&) = delete;
    AnimationClipLibrary(AnimationClipLibrary&&) noexcept = default;
    AnimationClipLibrary& operator=(AnimationClipLibrary&&) noexcept = default;

    bool load(const AnimationSet& set, std::string& error);
    void clear() noexcept;

    int count() const noexcept { return static_cast<int>(_entries.size()); }
    bool empty() const noexcept { return _entries.empty(); }

    const Clip* clip(int index) const noexcept;
    const ClipAssetInfo* info(int index) const noexcept;
    const Clip* find(std::string_view name) const noexcept;
    const ClipAssetInfo* findInfo(std::string_view name) const noexcept;

private:
    struct Entry {
        ClipAssetInfo info;
        std::unique_ptr<Clip> clip;
    };

    std::vector<Entry> _entries;
};

}

