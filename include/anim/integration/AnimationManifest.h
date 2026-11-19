#pragma once

#include <string>
#include <string_view>

#include "anim/integration/AnimationAssets.h"

namespace anim {

bool loadAnimationManifest(std::string_view text, AnimationSet& out, std::string& error);
bool loadAnimationManifestFile(const std::string& path, AnimationSet& out, std::string& error);

}  // namespace anim
