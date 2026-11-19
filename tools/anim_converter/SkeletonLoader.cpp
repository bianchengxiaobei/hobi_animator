#include "SkeletonLoader.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "anim/core/Hash.h"
#include "anim/integration/Json.h"

namespace anim::converter {
namespace {

Vec3 readVec3(const json::Value* value, const Vec3& fallback) {
    if (value == nullptr || !value->isArray() || value->array.size() < 3) return fallback;
    return Vec3(static_cast<float>(value->array[0].number),
                static_cast<float>(value->array[1].number),
                static_cast<float>(value->array[2].number));
}

Quat readQuat(const json::Value* value, const Quat& fallback) {
    if (value == nullptr || !value->isArray() || value->array.size() < 4) return fallback;
    return quatFromXYZW(static_cast<float>(value->array[0].number),
                        static_cast<float>(value->array[1].number),
                        static_cast<float>(value->array[2].number),
                        static_cast<float>(value->array[3].number));
}

bool readInverseBind(const json::Value* value, const std::string& boneName, Mat4& out,
                     std::string& error) {
    if (value == nullptr || !value->isArray() || value->array.size() != 16) {
        error = "bone '" + boneName + "' inverseBind must contain 16 numbers";
        return false;
    }

    for (const json::Value& component : value->array) {
        if (!component.isNumber() || !std::isfinite(component.number)) {
            error = "bone '" + boneName + "' inverseBind must contain finite numbers";
            return false;
        }
    }

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            out[column][row] = static_cast<float>(
                value->array[static_cast<std::size_t>(column * 4 + row)].number);
        }
    }
    return true;
}

}  // namespace

int SkeletonAsset::findByName(const std::string& name) const {
    int found = -1;
    for (int i = 0; i < static_cast<int>(boneNames.size()); ++i) {
        if (boneNames[static_cast<std::size_t>(i)] == name) {
            if (found >= 0) return -1;
            found = i;
        }
    }
    return found;
}

std::string SkeletonAsset::pathOf(int bone) const {
    if (bone < 0 || bone >= static_cast<int>(boneNames.size())) return {};
    const int parent = skeleton.bones[static_cast<std::size_t>(bone)].parent;
    if (parent < 0) return boneNames[static_cast<std::size_t>(bone)];
    return pathOf(parent) + "/" + boneNames[static_cast<std::size_t>(bone)];
}

int SkeletonAsset::resolvePath(const std::string& path) const {
    if (boneNames.empty()) return -1;
    if (path.empty()) return skeleton.rootIndex();

    for (int i = 0; i < static_cast<int>(boneNames.size()); ++i) {
        if (pathOf(i) == path) return i;
    }

    const std::size_t slash = path.find_last_of('/');
    const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
    return findByName(leaf);
}

bool loadSkeletonJson(const std::string& text, SkeletonAsset& out, std::string& error) {
    json::Value root;
    if (!json::parse(text, root, error)) return false;
    if (!root.isObject()) {
        error = "skeleton root must be an object";
        return false;
    }

    if (const json::Value* version = root.find("version");
        version != nullptr &&
        (!version->isNumber() || version->number != 1.0)) {
        error = "skeleton version must be 1";
        return false;
    }

    const json::Value* bones = root.find("bones");
    if (bones == nullptr || !bones->isArray()) {
        error = "skeleton JSON must contain a 'bones' array";
        return false;
    }

    out = SkeletonAsset{};
    std::vector<bool> hasInverseBind;
    try {
        for (const json::Value& bone : bones->array) {
            if (!bone.isObject()) {
                error = "bone entry must be an object";
                return false;
            }
            const json::Value* name = bone.find("name");
            if (name == nullptr || !name->isString()) {
                error = "bone entry requires a string 'name'";
                return false;
            }

            const json::Value* parent = bone.find("parent");
            if (parent != nullptr && !parent->isNumber()) {
                error = "bone '" + name->text + "' parent must be a number";
                return false;
            }
            const int parentIndex = parent != nullptr
                                        ? static_cast<int>(parent->number)
                                        : -1;
            if (parentIndex < -1 ||
                parentIndex >= static_cast<int>(out.boneNames.size())) {
                error = "bone '" + name->text +
                        "' parent must precede the child (topological order)";
                return false;
            }

            Transform rest;
            rest.translation = readVec3(bone.find("translation"), kZeroVec3);
            rest.rotation = safeNormalize(readQuat(bone.find("rotation"), kIdentityQuat));
            rest.scale = readVec3(bone.find("scale"), kOneVec3);

            Mat4 inverseBind(1.0f);
            const json::Value* inverseBindValue = bone.find("inverseBind");
            const bool hasBind = inverseBindValue != nullptr;
            if (hasBind && !readInverseBind(inverseBindValue, name->text, inverseBind,
                                            error)) {
                return false;
            }

            const int index =
                out.skeleton.addBone(hashString(name->text), parentIndex, rest, inverseBind);
            out.boneNames.push_back(name->text);
            hasInverseBind.push_back(hasBind);
            if (index != static_cast<int>(out.boneNames.size()) - 1) {
                error = "skeleton bone order mismatch";
                return false;
            }
        }
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    std::vector<Mat4> world(out.skeleton.bones.size(), Mat4(1.0f));
    for (std::size_t i = 0; i < out.skeleton.bones.size(); ++i) {
        const Bone& bone = out.skeleton.bones[i];
        world[i] = (bone.parent >= 0
                        ? world[static_cast<std::size_t>(bone.parent)]
                        : Mat4(1.0f)) *
                   bone.restLocal.toMatrix();
    }

    for (std::size_t i = 0; i < out.skeleton.bones.size(); ++i) {
        if (!hasInverseBind[i]) out.skeleton.bones[i].inverseBind = glm::inverse(world[i]);
    }

    return true;
}

bool loadSkeletonFile(const std::string& path, SkeletonAsset& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return loadSkeletonJson(stream.str(), out, error);
}

}  // namespace anim::converter
