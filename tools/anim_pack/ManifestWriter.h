#pragma once

#include <string>

#include "anim/integration/AnimationAssets.h"

namespace anim::pack {

bool writeManifestFile(const AnimationSet& manifest, const std::string& path,
                       std::string& error);

}  // namespace anim::pack
