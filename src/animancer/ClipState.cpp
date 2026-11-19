#include "anim/animancer/ClipState.h"

#include "anim/animancer/AnimancerPlayable.h"
#include "anim/graph/Nodes.h"

namespace anim {

ClipState::ClipState(AnimancerPlayable* root, const Clip& clip) : AnimancerState(root), _clip(&clip) {
    setKey(Key::fromPointer(_clip));
}

float ClipState::length() const { return _clip != nullptr ? _clip->length : 0.0f; }

bool ClipState::isLooping() const noexcept { return _clip != nullptr && _clip->looping; }

void ClipState::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    gatherClip(clips, _clip);
}

void ClipState::createPlayable() {
    if (_clip == nullptr || root() == nullptr || root()->skeleton() == nullptr) return;

    _playable = root()->graph().create<ClipNode>(*_clip, *root()->skeleton(), root()->avatar());
    auto* node = static_cast<ClipNode*>(root()->node(_playable));
    if (node != nullptr) {
        // 时间由 AnimancerState 统一管理，避免图节点重复推进。
        node->setAutoAdvanceTime(false);
        node->setTime(timeD());
        if (!isPlaying()) node->pause();
    }
}

AnimancerState* ClipState::clone(AnimancerPlayable* root) {
    return new ClipState(root, *_clip);
}

}  // namespace anim
