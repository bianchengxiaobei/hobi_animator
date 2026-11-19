#include "anim/hobi/RetargetClipState.h"

#include "anim/hobi/HobiPlayable.h"
#include "anim/avatar/HumanoidAvatar.h"
#include "anim/clip/Clip.h"
#include "anim/graph/Nodes.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

RetargetClipState::RetargetClipState(HobiPlayable* root, const Clip& clip,
                                     const Skeleton& sourceSkeleton,
                                     const HumanoidAvatar& sourceAvatar,
                                     const HumanoidAvatar& targetAvatar)
    : HobiState(root),
      _clip(&clip),
      _sourceSkeleton(&sourceSkeleton),
      _sourceAvatar(&sourceAvatar),
      _targetAvatar(&targetAvatar) {
    setKey(Key::fromPointer(_clip));
}

float RetargetClipState::length() const {
    return _clip != nullptr ? _clip->length : 0.0f;
}

bool RetargetClipState::isLooping() const noexcept {
    return _clip != nullptr && _clip->looping;
}

void RetargetClipState::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    gatherClip(clips, _clip);
}

void RetargetClipState::createPlayable() {
    if (_clip == nullptr || _clip->type != ClipType::BoneLocal ||
        _sourceSkeleton == nullptr || _sourceAvatar == nullptr || _targetAvatar == nullptr ||
        root() == nullptr || root()->skeleton() == nullptr) {
        return;
    }

    _playable = root()->graph().create<RetargetClipNode>(
        *_clip, *_sourceSkeleton, *_sourceAvatar, *root()->skeleton(), *_targetAvatar);
    auto* node = static_cast<RetargetClipNode*>(root()->node(_playable));
    if (node != nullptr) {
        node->setAutoAdvanceTime(false);
        node->setTime(timeD());
        if (!isPlaying()) node->pause();
    }
}

HobiState* RetargetClipState::clone(HobiPlayable* root) {
    return new RetargetClipState(root, *_clip, *_sourceSkeleton, *_sourceAvatar,
                                 *_targetAvatar);
}

}  // namespace anim
