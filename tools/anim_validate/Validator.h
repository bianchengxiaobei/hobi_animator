#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Report.h"
#include "anim/clip/Clip.h"
#include "anim/core/Span.h"
#include "anim/integration/AnimationAssets.h"
#include "anim/skeleton/Skeleton.h"

namespace anim::validate {

struct ClipValidationOptions {
    std::string file;
    std::string clipName;
    const Skeleton* skeleton = nullptr;
};

struct ManifestValidationOptions {
    std::string file;
    const Skeleton* skeleton = nullptr;
    const std::vector<std::string>* boneNames = nullptr;
};

void validateClip(const Clip& clip, const ClipValidationOptions& options, Report& report);
std::unique_ptr<Clip> validateClipBytes(ByteSpan data, const ClipValidationOptions& options,
                                        Report& report);
void validateSkeleton(const Skeleton& skeleton, const std::string& file,
                      const std::vector<std::string>* boneNames, Report& report);
void validateManifest(const AnimationSet& manifest, const ManifestValidationOptions& options,
                      Report& report);
void validateClipManifest(const Clip& clip, const ClipAssetInfo& info,
                          std::size_t clipIndex,
                          const ManifestValidationOptions& options, Report& report);

}  // namespace anim::validate
