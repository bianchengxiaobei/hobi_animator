#pragma once

#include <string>

#include "GltfImporter.h"
#include "anim/clip/Clip.h"
#include "anim/integration/AnimationAssets.h"

namespace anim::pack {

bool buildBoneLocalClip(const ImportedAnimation& animation, const ImportedAsset& asset,
                        const ClipAssetInfo& info, bool mirrored, BoneLocalClip& out,
                        std::string& error);

}  // namespace anim::pack
