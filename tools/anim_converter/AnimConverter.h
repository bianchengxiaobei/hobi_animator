#pragma once

#include <memory>
#include <string>
#include <vector>

#include "AvatarLoader.h"
#include "SkeletonLoader.h"
#include "YamlAnimParser.h"
#include "anim/clip/Clip.h"

namespace anim::converter {

struct ConvertOptions {
    const SkeletonAsset* skeleton = nullptr;
    const AvatarConfig* avatar = nullptr;
};

struct ConvertResult {
    std::unique_ptr<Clip> clip;
    std::vector<std::string> warnings;
    std::string error;

    bool ok() const noexcept { return clip != nullptr && error.empty(); }
};

ConvertResult convertParsed(const ParsedClip& parsed, const ConvertOptions& options);
ConvertResult convertFile(const std::string& path, const ConvertOptions& options);

}  // namespace anim::converter
