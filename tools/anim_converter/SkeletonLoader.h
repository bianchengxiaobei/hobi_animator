#pragma once

#include <string>
#include <vector>

#include "anim/skeleton/Skeleton.h"

namespace anim::converter {

struct SkeletonAsset {
    Skeleton skeleton;
    std::vector<std::string> boneNames;

    int findByName(const std::string& name) const;
    int resolvePath(const std::string& path) const;
    std::string pathOf(int bone) const;
};

bool loadSkeletonFile(const std::string& path, SkeletonAsset& out, std::string& error);
bool loadSkeletonJson(const std::string& json, SkeletonAsset& out, std::string& error);

}  // namespace anim::converter
