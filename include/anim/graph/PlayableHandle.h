#pragma once

#include <string>

#include "anim/core/Handle.h"

// Playable 图句柄。
//
// 文档：docs/06-Playable图.md §2
namespace anim {

struct PlayableNodeTag {};
using PlayableHandle = Handle<PlayableNodeTag>;

enum class PlayableNodeType : uint8_t {
    Script = 0,
    Mixer,
    LayerMixer,
    Clip,
    RetargetClip,
    ScriptPose,
};

const char* toString(PlayableNodeType type) noexcept;

}  // namespace anim
