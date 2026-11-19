#include "anim/hobi/ClipState.h"

#include "anim/hobi/HobiPlayable.h"
#include "anim/graph/Nodes.h"

namespace anim {

ClipState::ClipState(HobiPlayable* root, const Clip& clip) : HobiState(root), _clip(&clip) {
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
        // 时间由 HobiState 统一管理，避免图节点重复推进。
        node->setAutoAdvanceTime(false);
        node->setTime(timeD());
        if (!isPlaying()) node->pause();
    }
}

HobiState* ClipState::clone(HobiPlayable* root) {
    return new ClipState(root, *_clip);
}

}  // namespace anim
