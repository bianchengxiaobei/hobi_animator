#pragma once

#include <string>
#include <vector>

#include "anim/core/Math.h"
#include "anim/core/Transform.h"

namespace anim::pack {

enum class AnimationPath {
    Translation,
    Rotation,
    Scale,
};

enum class Interpolation {
    Linear,
    Step,
    CubicSpline,
};

struct ImportedKey {
    float time = 0.0f;
    float value[4]{};
    float inTangent[4]{};
    float outTangent[4]{};
};

struct ImportedChannel {
    int boneIndex = -1;
    AnimationPath path = AnimationPath::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<ImportedKey> keys;
};

struct ImportedBone {
    std::string name;
    int parent = -1;
    Transform restLocal;
    Mat4 inverseBind{1.0f};
};

struct ImportedAnimation {
    std::string name;
    float length = 0.0f;
    std::vector<ImportedChannel> channels;
};

struct ImportedAsset {
    std::vector<ImportedBone> bones;
    std::vector<ImportedAnimation> animations;
};

bool importGltfFile(const std::string& path, ImportedAsset& out,
                    std::vector<std::string>& warnings, std::string& error);

}  // namespace anim::pack
