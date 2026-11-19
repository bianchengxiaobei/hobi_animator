#pragma once

#include <algorithm>
#include <vector>

namespace anim {

struct Clip;

class IAnimationClipCollection {
public:
    virtual ~IAnimationClipCollection() = default;
    virtual void gatherAnimationClips(std::vector<const Clip*>& clips) const = 0;
};

inline void gatherClip(std::vector<const Clip*>& clips, const Clip* clip) {
    if (clip == nullptr) return;
    if (std::find(clips.begin(), clips.end(), clip) != clips.end()) return;
    clips.push_back(clip);
}

}  // namespace anim
