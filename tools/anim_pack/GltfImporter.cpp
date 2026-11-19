#include "GltfImporter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cgltf.h"

namespace anim::pack {
namespace {

constexpr float kDecomposeTolerance = 1e-4f;

struct CgltfDeleter {
    void operator()(cgltf_data* data) const {
        if (data != nullptr) cgltf_free(data);
    }
};

using CgltfPtr = std::unique_ptr<cgltf_data, CgltfDeleter>;

std::string nodeLabel(const cgltf_node* node, std::size_t index) {
    if (node != nullptr && node->name != nullptr && node->name[0] != '\0') {
        return node->name;
    }
    return "node[" + std::to_string(index) + "]";
}

Mat4 matrixFromFloats(const cgltf_float* values) {
    Mat4 result(1.0f);
    std::memcpy(&result[0][0], values, sizeof(float) * 16);
    return result;
}

bool isFiniteMatrix(const Mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) return false;
        }
    }
    return true;
}

bool decomposeMatrix(const Mat4& matrix, const std::string& context, Transform& out,
                     std::string& error) {
    if (!isFiniteMatrix(matrix)) {
        error = context + " contains a non-finite transform";
        return false;
    }

    out = Transform::fromMatrix(matrix);
    if (!std::isfinite(out.translation.x) || !std::isfinite(out.translation.y) ||
        !std::isfinite(out.translation.z) || !isFinite(out.rotation) ||
        !std::isfinite(out.scale.x) || !std::isfinite(out.scale.y) ||
        !std::isfinite(out.scale.z)) {
        error = context + " cannot be decomposed into TRS";
        return false;
    }

    const Mat4 rebuilt = out.toMatrix();
    float maxDifference = 0.0f;
    float maxMagnitude = 1.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            maxDifference =
                std::max(maxDifference, std::abs(rebuilt[column][row] - matrix[column][row]));
            maxMagnitude = std::max(maxMagnitude, std::abs(matrix[column][row]));
        }
    }

    if (maxDifference > kDecomposeTolerance * maxMagnitude) {
        error = context + " contains shear or an unsupported non-TRS transform";
        return false;
    }
    return true;
}

bool nodeLocalMatrix(const cgltf_node* node, const std::string& context, Mat4& out,
                     std::string& error) {
    cgltf_float values[16]{};
    cgltf_node_transform_local(node, values);
    out = matrixFromFloats(values);
    if (!isFiniteMatrix(out)) {
        error = context + " contains a non-finite transform";
        return false;
    }
    return true;
}

bool nodeLocalTransform(const cgltf_node* node, const std::string& context, Transform& out,
                        std::string& error) {
    if (node->has_matrix) {
        Mat4 matrix;
        if (!nodeLocalMatrix(node, context, matrix, error)) return false;
        return decomposeMatrix(matrix, context, out, error);
    }

    out.translation = Vec3(node->translation[0], node->translation[1], node->translation[2]);
    out.rotation = safeNormalize(quatFromXYZW(node->rotation[0], node->rotation[1],
                                              node->rotation[2], node->rotation[3]));
    out.scale = Vec3(node->scale[0], node->scale[1], node->scale[2]);

    if (!std::isfinite(out.translation.x) || !std::isfinite(out.translation.y) ||
        !std::isfinite(out.translation.z) || !isFinite(out.rotation) ||
        !std::isfinite(out.scale.x) || !std::isfinite(out.scale.y) ||
        !std::isfinite(out.scale.z)) {
        error = context + " contains a non-finite TRS";
        return false;
    }
    return true;
}

bool readTimes(const cgltf_accessor* accessor, const std::string& context,
               std::vector<float>& out, std::string& error) {
    if (accessor == nullptr) {
        error = context + " has no input accessor";
        return false;
    }
    if (accessor->type != cgltf_type_scalar) {
        error = context + " input accessor must be SCALAR";
        return false;
    }
    if (accessor->count == 0) {
        error = context + " input accessor is empty";
        return false;
    }

    const cgltf_size floatCount = cgltf_accessor_unpack_floats(accessor, nullptr, 0);
    if (floatCount == 0) {
        error = context + " input accessor cannot be read";
        return false;
    }

    std::vector<float> values(floatCount);
    if (cgltf_accessor_unpack_floats(accessor, values.data(), floatCount) != floatCount) {
        error = context + " input accessor cannot be read";
        return false;
    }

    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i]) || values[i] < 0.0f) {
            error = context + " contains a negative or non-finite time";
            return false;
        }
        if (i > 0 && values[i] <= values[i - 1]) {
            error = context + " times must be strictly increasing";
            return false;
        }
    }

    out = std::move(values);
    return true;
}

bool readAccessor(const cgltf_accessor* accessor, const std::string& context,
                  std::vector<float>& out, std::string& error) {
    if (accessor == nullptr) {
        error = context + " has no output accessor";
        return false;
    }

    const cgltf_size floatCount = cgltf_accessor_unpack_floats(accessor, nullptr, 0);
    if (floatCount == 0) {
        error = context + " output accessor is empty or cannot be read";
        return false;
    }

    std::vector<float> values(floatCount);
    if (cgltf_accessor_unpack_floats(accessor, values.data(), floatCount) != floatCount) {
        error = context + " output accessor cannot be read";
        return false;
    }

    for (float value : values) {
        if (!std::isfinite(value)) {
            error = context + " contains a non-finite value";
            return false;
        }
    }

    out = std::move(values);
    return true;
}

const char* pathName(AnimationPath path) {
    switch (path) {
        case AnimationPath::Translation:
            return "translation";
        case AnimationPath::Rotation:
            return "rotation";
        case AnimationPath::Scale:
            return "scale";
    }
    return "unknown";
}

void applyPrefix(ImportedChannel& channel, const Transform& prefix) {
    const Quat rotation = safeNormalize(prefix.rotation);
    for (ImportedKey& key : channel.keys) {
        if (channel.path == AnimationPath::Translation) {
            auto transformVector = [&](float values[4]) {
                const Vec3 value = prefix.translation +
                                   rotation * (prefix.scale * Vec3(values[0], values[1],
                                                                   values[2]));
                values[0] = value.x;
                values[1] = value.y;
                values[2] = value.z;
            };
            transformVector(key.value);
            transformVector(key.inTangent);
            transformVector(key.outTangent);
        } else if (channel.path == AnimationPath::Rotation) {
            Quat value = quatFromXYZW(key.value[0], key.value[1], key.value[2],
                                      key.value[3]);
            value = safeNormalize(rotation * value);
            key.value[0] = value.x;
            key.value[1] = value.y;
            key.value[2] = value.z;
            key.value[3] = value.w;

            auto transformTangent = [&](float values[4]) {
                const Quat tangent =
                    rotation * quatFromXYZW(values[0], values[1], values[2], values[3]);
                values[0] = tangent.x;
                values[1] = tangent.y;
                values[2] = tangent.z;
                values[3] = tangent.w;
            };
            transformTangent(key.inTangent);
            transformTangent(key.outTangent);
        } else {
            for (int component = 0; component < 3; ++component) {
                key.value[component] *= prefix.scale[component];
                key.inTangent[component] *= prefix.scale[component];
                key.outTangent[component] *= prefix.scale[component];
            }
        }
    }
}

}  // namespace

bool importGltfFile(const std::string& path, ImportedAsset& out,
                    std::vector<std::string>& warnings, std::string& error) {
    out = ImportedAsset{};
    warnings.clear();
    error.clear();

    cgltf_options options{};
    cgltf_data* raw = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &raw);
    if (result != cgltf_result_success) {
        error = "cannot parse '" + path + "' (cgltf result " +
                std::to_string(static_cast<int>(result)) + ")";
        return false;
    }
    CgltfPtr data(raw);

    result = cgltf_load_buffers(&options, data.get(), path.c_str());
    if (result != cgltf_result_success) {
        error = "cannot load buffers for '" + path + "' (cgltf result " +
                std::to_string(static_cast<int>(result)) + ")";
        return false;
    }

    result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        error = "invalid glTF '" + path + "' (cgltf result " +
                std::to_string(static_cast<int>(result)) + ")";
        return false;
    }

    if (data->skins_count == 0) {
        error = "'" + path + "' contains no skin";
        return false;
    }
    if (data->skins_count > 1) {
        warnings.push_back("multiple skins found; using skin[0]");
    }

    const cgltf_skin* skin = &data->skins[0];
    if (skin->joints_count == 0) {
        error = "skin[0] contains no joints";
        return false;
    }

    const std::size_t jointCount = static_cast<std::size_t>(skin->joints_count);
    std::vector<const cgltf_node*> joints(jointCount, nullptr);
    std::unordered_map<const cgltf_node*, std::size_t> jointOrdinals;
    jointOrdinals.reserve(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const cgltf_node* joint = skin->joints[i];
        if (joint == nullptr) {
            error = "skin[0].joints[" + std::to_string(i) + "] is null";
            return false;
        }
        if (!jointOrdinals.emplace(joint, i).second) {
            error = "skin[0] contains a duplicate joint";
            return false;
        }
        joints[i] = joint;
    }

    std::vector<int> parentOrdinal(jointCount, -1);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const cgltf_node* parent = joints[i]->parent;
        while (parent != nullptr && jointOrdinals.find(parent) == jointOrdinals.end()) {
            parent = parent->parent;
        }
        if (parent != nullptr) {
            parentOrdinal[i] = static_cast<int>(jointOrdinals.at(parent));
        }
    }

    std::vector<int> depth(jointCount, -1);
    std::vector<char> visiting(jointCount, 0);
    std::function<bool(std::size_t)> computeDepth = [&](std::size_t index) {
        if (depth[index] >= 0) return true;
        if (visiting[index] != 0) {
            error = "skin[0] joint hierarchy contains a cycle";
            return false;
        }
        visiting[index] = 1;
        if (parentOrdinal[index] < 0) {
            depth[index] = 0;
        } else {
            const std::size_t parent = static_cast<std::size_t>(parentOrdinal[index]);
            if (!computeDepth(parent)) return false;
            depth[index] = depth[parent] + 1;
        }
        visiting[index] = 0;
        return true;
    };

    for (std::size_t i = 0; i < jointCount; ++i) {
        if (!computeDepth(i)) return false;
    }

    std::vector<std::size_t> sorted(jointCount);
    std::iota(sorted.begin(), sorted.end(), std::size_t{0});
    std::stable_sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
        if (depth[a] != depth[b]) return depth[a] < depth[b];
        return a < b;
    });

    std::vector<int> originalToBone(jointCount, -1);
    for (std::size_t boneIndex = 0; boneIndex < sorted.size(); ++boneIndex) {
        originalToBone[sorted[boneIndex]] = static_cast<int>(boneIndex);
    }

    std::vector<Transform> rest(jointCount);
    std::vector<Transform> prefixes(jointCount, Transform::identity());
    std::vector<bool> hasPrefix(jointCount, false);
    std::vector<std::string> names(jointCount);
    std::unordered_set<std::string> usedNames;
    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::string context = "joint '" + nodeLabel(joints[i], i) + "'";
        names[i] = nodeLabel(joints[i], i);
        if (joints[i]->name == nullptr || joints[i]->name[0] == '\0') {
            warnings.push_back("joint " + std::to_string(i) + " has no name; using '" +
                               names[i] + "'");
        }
        if (!usedNames.insert(names[i]).second) {
            error = "duplicate joint name '" + names[i] + "'";
            return false;
        }

        Transform local;
        if (!nodeLocalTransform(joints[i], context, local, error)) return false;

        Transform prefix = Transform::identity();
        bool hasNonJointAncestor = false;
        for (const cgltf_node* parent = joints[i]->parent;
             parent != nullptr && jointOrdinals.find(parent) == jointOrdinals.end();
             parent = parent->parent) {
            Transform parentTransform;
            if (!nodeLocalTransform(parent, "non-joint ancestor of " + context,
                                    parentTransform, error)) {
                return false;
            }
            prefix = Transform::compose(parentTransform, prefix);
            hasNonJointAncestor = true;
        }

        if (!hasNonJointAncestor) {
            rest[i] = local;
            continue;
        }

        Mat4 merged;
        if (!nodeLocalMatrix(joints[i], context, merged, error)) return false;
        for (const cgltf_node* parent = joints[i]->parent;
             parent != nullptr && jointOrdinals.find(parent) == jointOrdinals.end();
             parent = parent->parent) {
            Mat4 parentMatrix;
            if (!nodeLocalMatrix(parent, "non-joint ancestor of " + context, parentMatrix,
                                 error)) {
                return false;
            }
            merged = parentMatrix * merged;
        }

        Transform composed = Transform::compose(prefix, local);
        const Mat4 composedMatrix = composed.toMatrix();
        float maxDifference = 0.0f;
        float maxMagnitude = 1.0f;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                maxDifference = std::max(
                    maxDifference, std::abs(composedMatrix[column][row] - merged[column][row]));
                maxMagnitude = std::max(maxMagnitude, std::abs(merged[column][row]));
            }
        }
        if (maxDifference > kDecomposeTolerance * maxMagnitude) {
            error = "non-joint parent chain of " + context +
                    " cannot be represented as a local TRS";
            return false;
        }

        prefixes[i] = prefix;
        hasPrefix[i] = true;
        rest[i] = composed;
    }

    std::vector<Mat4> inverseBind(jointCount, Mat4(1.0f));
    const bool hasInverseBind = skin->inverse_bind_matrices != nullptr;
    if (hasInverseBind) {
        const cgltf_accessor* accessor = skin->inverse_bind_matrices;
        if (accessor->type != cgltf_type_mat4) {
            error = "skin[0].inverseBindMatrices must be MAT4";
            return false;
        }
        if (accessor->count != skin->joints_count) {
            error = "skin[0].inverseBindMatrices count must match skin joints";
            return false;
        }

        const cgltf_size floatCount = cgltf_accessor_unpack_floats(accessor, nullptr, 0);
        if (floatCount != static_cast<cgltf_size>(jointCount * 16)) {
            error = "skin[0].inverseBindMatrices cannot be read";
            return false;
        }

        std::vector<float> values(floatCount);
        if (cgltf_accessor_unpack_floats(accessor, values.data(), floatCount) != floatCount) {
            error = "skin[0].inverseBindMatrices cannot be read";
            return false;
        }

        for (std::size_t i = 0; i < jointCount; ++i) {
            inverseBind[i] = matrixFromFloats(&values[i * 16]);
            if (!isFiniteMatrix(inverseBind[i])) {
                error = "skin[0].inverseBindMatrices contains a non-finite matrix";
                return false;
            }
        }
    } else {
        warnings.push_back("skin[0] has no inverseBindMatrices; computing from rest pose");
    }

    out.bones.reserve(jointCount);
    for (std::size_t boneIndex = 0; boneIndex < sorted.size(); ++boneIndex) {
        const std::size_t original = sorted[boneIndex];
        ImportedBone bone;
        bone.name = names[original];
        bone.parent = parentOrdinal[original] >= 0
                          ? originalToBone[static_cast<std::size_t>(parentOrdinal[original])]
                          : -1;
        bone.restLocal = rest[original];
        bone.inverseBind = inverseBind[original];
        out.bones.push_back(std::move(bone));
    }

    if (!hasInverseBind) {
        std::vector<Mat4> world(jointCount, Mat4(1.0f));
        for (std::size_t i = 0; i < out.bones.size(); ++i) {
            const ImportedBone& bone = out.bones[i];
            world[i] = (bone.parent >= 0 ? world[static_cast<std::size_t>(bone.parent)]
                                         : Mat4(1.0f)) *
                       bone.restLocal.toMatrix();
            out.bones[i].inverseBind = glm::inverse(world[i]);
        }
    }

    std::vector<int> nodeToBone(static_cast<std::size_t>(data->nodes_count), -1);
    std::vector<int> nodeToJointOriginal(static_cast<std::size_t>(data->nodes_count), -1);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::size_t nodeIndex =
            static_cast<std::size_t>(joints[i] - data->nodes);
        nodeToBone[nodeIndex] = originalToBone[i];
        nodeToJointOriginal[nodeIndex] = static_cast<int>(i);
    }

    out.animations.reserve(static_cast<std::size_t>(data->animations_count));
    for (cgltf_size animationIndex = 0; animationIndex < data->animations_count;
         ++animationIndex) {
        const cgltf_animation* animation = &data->animations[animationIndex];
        if (animation->name == nullptr || animation->name[0] == '\0') {
            error = "animations[" + std::to_string(animationIndex) + "] has no name";
            return false;
        }

        ImportedAnimation imported;
        imported.name = animation->name;
        std::unordered_set<std::uint64_t> channelKeys;

        for (cgltf_size i = 0; i < animation->channels_count; ++i) {
            const cgltf_animation_channel* channel = &animation->channels[i];
            const std::string context =
                "animation '" + imported.name + "' channel " + std::to_string(i);

            if (channel->target_node == nullptr) {
                error = context + " has no target node";
                return false;
            }
            if (channel->target_path == cgltf_animation_path_type_weights) {
                error = context + " uses unsupported morph target weights";
                return false;
            }

            AnimationPath path;
            if (channel->target_path == cgltf_animation_path_type_translation) {
                path = AnimationPath::Translation;
            } else if (channel->target_path == cgltf_animation_path_type_rotation) {
                path = AnimationPath::Rotation;
            } else if (channel->target_path == cgltf_animation_path_type_scale) {
                path = AnimationPath::Scale;
            } else {
                error = context + " has an unsupported target path";
                return false;
            }

            const std::size_t nodeIndex =
                static_cast<std::size_t>(channel->target_node - data->nodes);
            if (nodeIndex >= nodeToBone.size() || nodeToBone[nodeIndex] < 0) {
                error = context + " targets non-joint node '" +
                        nodeLabel(channel->target_node, nodeIndex) + "'";
                return false;
            }

            if (channel->sampler == nullptr) {
                error = context + " has no sampler";
                return false;
            }
            const cgltf_animation_sampler* sampler = channel->sampler;

            Interpolation interpolation;
            if (sampler->interpolation == cgltf_interpolation_type_linear) {
                interpolation = Interpolation::Linear;
            } else if (sampler->interpolation == cgltf_interpolation_type_step) {
                interpolation = Interpolation::Step;
            } else if (sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
                interpolation = Interpolation::CubicSpline;
            } else {
                error = context + " uses an unsupported interpolation";
                return false;
            }

            const std::uint64_t channelKey =
                static_cast<std::uint64_t>(nodeIndex) * 3 +
                static_cast<std::uint64_t>(path);
            if (!channelKeys.insert(channelKey).second) {
                error = context + " duplicates " + pathName(path) + " for the same node";
                return false;
            }

            std::vector<float> times;
            if (!readTimes(sampler->input, context, times, error)) return false;

            const std::size_t components = path == AnimationPath::Rotation ? 4u : 3u;
            const cgltf_type expectedType =
                path == AnimationPath::Rotation ? cgltf_type_vec4 : cgltf_type_vec3;
            if (sampler->output == nullptr) {
                error = context + " has no output accessor";
                return false;
            }
            if (sampler->output->type != expectedType) {
                error = context + " output accessor type does not match " + pathName(path);
                return false;
            }
            const cgltf_size expectedCount =
                interpolation == Interpolation::CubicSpline
                    ? static_cast<cgltf_size>(times.size() * 3)
                    : static_cast<cgltf_size>(times.size());
            if (sampler->output->count != expectedCount) {
                error = context + " output key count does not match input key count";
                return false;
            }

            std::vector<float> values;
            if (!readAccessor(sampler->output, context, values, error)) return false;
            const std::size_t expectedFloats =
                static_cast<std::size_t>(expectedCount) * components;
            if (values.size() != expectedFloats) {
                error = context + " output accessor has an unexpected size";
                return false;
            }

            ImportedChannel importedChannel;
            importedChannel.boneIndex = nodeToBone[nodeIndex];
            importedChannel.path = path;
            importedChannel.interpolation = interpolation;
            importedChannel.keys.resize(times.size());

            for (std::size_t keyIndex = 0; keyIndex < times.size(); ++keyIndex) {
                ImportedKey& key = importedChannel.keys[keyIndex];
                key.time = times[keyIndex];

                const std::size_t valueBase =
                    interpolation == Interpolation::CubicSpline
                        ? (keyIndex * 3 + 1) * components
                        : keyIndex * components;
                for (std::size_t component = 0; component < components; ++component) {
                    key.value[component] = values[valueBase + component];
                }

                if (interpolation == Interpolation::CubicSpline) {
                    const std::size_t inBase = keyIndex * 3 * components;
                    const std::size_t outBase = (keyIndex * 3 + 2) * components;
                    for (std::size_t component = 0; component < components; ++component) {
                        key.inTangent[component] = values[inBase + component];
                        key.outTangent[component] = values[outBase + component];
                    }
                }
            }

            const int originalJoint = nodeToJointOriginal[nodeIndex];
            if (originalJoint < 0) {
                error = context + " cannot resolve its joint index";
                return false;
            }
            if (hasPrefix[static_cast<std::size_t>(originalJoint)]) {
                applyPrefix(importedChannel,
                            prefixes[static_cast<std::size_t>(originalJoint)]);
            }

            imported.length = std::max(imported.length, times.back());
            imported.channels.push_back(std::move(importedChannel));
        }

        if (imported.channels.empty()) {
            error = "animation '" + imported.name + "' contains no channels";
            return false;
        }

        std::stable_sort(imported.channels.begin(), imported.channels.end(),
                         [](const ImportedChannel& a, const ImportedChannel& b) {
                             if (a.boneIndex != b.boneIndex) return a.boneIndex < b.boneIndex;
                             return static_cast<int>(a.path) < static_cast<int>(b.path);
                         });
        out.animations.push_back(std::move(imported));
    }

    return true;
}

}  // namespace anim::pack
