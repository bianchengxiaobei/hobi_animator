#include "anim/skeleton/Skeleton.h"

#include "anim/core/Assert.h"

namespace anim {

int Skeleton::addBone(StringId name, int32_t parent, const Transform& restLocal,
                      const Mat4& inverseBind) {
    ANIM_CHECK_MSG(parent < static_cast<int32_t>(bones.size()),
                   "parent index must precede the child (topological order)");
    ANIM_CHECK_MSG(parent >= -1, "invalid parent index");
    ANIM_CHECK_MSG(_lookup.find(name) == _lookup.end(), "duplicate bone name");

    Bone bone;
    bone.name = name;
    bone.parent = parent;
    bone.restLocal = restLocal;
    bone.inverseBind = inverseBind;

    const int index = static_cast<int>(bones.size());
    bones.push_back(bone);
    _lookup.emplace(name, index);
    return index;
}

int Skeleton::find(StringId name) const noexcept {
    auto it = _lookup.find(name);
    return it == _lookup.end() ? -1 : it->second;
}

int Skeleton::rootIndex() const noexcept {
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].parent < 0) return static_cast<int>(i);
    }
    return -1;
}

const Transform& Skeleton::restLocal(int index) const {
    ANIM_CHECK_MSG(index >= 0 && index < boneCount(), "bone index out of range");
    return bones[static_cast<std::size_t>(index)].restLocal;
}

const Bone& Skeleton::bone(int index) const {
    ANIM_CHECK_MSG(index >= 0 && index < boneCount(), "bone index out of range");
    return bones[static_cast<std::size_t>(index)];
}

void Skeleton::clear() noexcept {
    bones.clear();
    _lookup.clear();
}

void Skeleton::reserve(std::size_t count) {
    bones.reserve(count);
    _lookup.reserve(count);
}

bool Skeleton::isTopological() const noexcept {
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const int32_t parent = bones[i].parent;
        if (parent >= static_cast<int32_t>(i)) return false;
    }
    return true;
}

}  // namespace anim
