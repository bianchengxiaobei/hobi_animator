#pragma once

#include <string>

#include "GltfImporter.h"

namespace anim::pack {

bool writeSkeletonFile(const ImportedAsset& asset, const std::string& path,
                       std::string& error);

}  // namespace anim::pack
