#pragma once

#include <string>

#include "AvatarLoader.h"
#include "SkeletonLoader.h"
#include "anim/avatar/HumanoidAvatar.h"

namespace anim::converter {

// Resolves AvatarConfig bone names against the skeleton, copies the rest pose, and
// applies preRot/postRot/limitsOverride emitted by anim_calibrate.
bool buildHumanoidAvatar(const SkeletonAsset& skeleton, const AvatarConfig& config,
                         HumanoidAvatar& out, std::string& error);

}  // namespace anim::converter
