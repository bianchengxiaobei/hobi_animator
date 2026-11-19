#include "FbxLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <unordered_map>

#include "anim/core/Hash.h"
#include "FbxMetaParser.h"

namespace demo {
namespace {

anim::Mat4 toGlm(const aiMatrix4x4& matrix) {
    anim::Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result[column][row] = matrix[row][column];
        }
    }
    return result;
}

anim::Transform decompose(const anim::Mat4& value) {
    anim::Vec3 scale(1.0f);
    anim::Quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    anim::Vec3 translation(0.0f);
    anim::Vec3 skew(0.0f);
    anim::Vec4 perspective(0.0f);
    glm::decompose(value, scale, rotation, translation, skew, perspective);

    anim::Transform result;
    result.translation = translation;
    result.rotation = anim::safeNormalize(rotation);
    result.scale = scale;
    return result;
}

anim::Mat4 mirrorX() {
    anim::Mat4 result(1.0f);
    result[0][0] = -1.0f;
    return result;
}

anim::Transform mirrorX(const aiMatrix4x4& matrix) {
    const anim::Mat4 mirror = mirrorX();
    return decompose(mirror * toGlm(matrix) * mirror);
}

std::string nodeName(const aiNode* node, int index) {
    std::string name = node->mName.C_Str();
    if (name.empty()) name = "__node_" + std::to_string(index);
    return name;
}

bool isAssimpHelper(const std::string& name) {
    return name.find("_$AssimpFbx$") != std::string::npos;
}

const aiNode* findMeshNode(const aiNode* node, unsigned meshIndex) {
    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        if (node->mMeshes[i] == meshIndex) return node;
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        const aiNode* found = findMeshNode(node->mChildren[i], meshIndex);
        if (found != nullptr) return found;
    }
    return nullptr;
}

}  // namespace

bool loadFbx(const std::string& fbxPath, const std::string& metaPath, Model& out,
             std::string& error) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        fbxPath, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                     aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights |
                     aiProcess_GlobalScale | aiProcess_ValidateDataStructure);
    if (scene == nullptr || scene->mRootNode == nullptr) {
        error = importer.GetErrorString();
        return false;
    }

    out = Model{};
    out.skeleton.addBone(anim::hashString("MeshRoot"), -1, anim::Transform{});
    out.boneNames.push_back("MeshRoot");

    std::unordered_map<std::string, int> nameToBone;
    const anim::Mat4 mirror = mirrorX();

    int nodeIndex = 0;
    std::function<int(aiNode*, int, const aiMatrix4x4&, const aiMatrix4x4&)> addNode =
        [&](aiNode* node, int parent, const aiMatrix4x4& parentNamedWorld,
            const aiMatrix4x4& parentWorld) -> int {
            const aiMatrix4x4 world = parentWorld * node->mTransformation;
            const std::string name = nodeName(node, nodeIndex++);
            int index = parent;
            const aiMatrix4x4 childNamedWorld = isAssimpHelper(name) ? parentNamedWorld : world;

            if (!isAssimpHelper(name)) {
                aiMatrix4x4 inverseParent = parentNamedWorld;
                inverseParent.Inverse();
                const aiMatrix4x4 local = inverseParent * world;
                const anim::Transform rest = mirrorX(local);
                const std::string base = name;
                std::string storedName = name;

                index = out.skeleton.addBone(anim::hashString(storedName), parent, rest);
                for (int suffix = 2; index < 0 && suffix < 1000; ++suffix) {
                    storedName = base + "#" + std::to_string(suffix);
                    index = out.skeleton.addBone(anim::hashString(storedName), parent, rest);
                }
                if (index >= 0) {
                    out.boneNames.push_back(storedName);
                    nameToBone.emplace(storedName, index);
                    nameToBone.emplace(name, index);
                }
            }

            for (unsigned i = 0; i < node->mNumChildren; ++i) {
                addNode(node->mChildren[i], index, childNamedWorld, world);
            }
            return index;
        };

    addNode(scene->mRootNode, 0, aiMatrix4x4{}, aiMatrix4x4{});

    for (unsigned meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        const aiNode* meshNode = findMeshNode(scene->mRootNode, meshIndex);
        std::unordered_map<unsigned, int> boneRemap;
        int unskinnedBone = -1;
        if (mesh->mNumBones == 0 && meshNode != nullptr) {
            const auto found = nameToBone.find(meshNode->mName.C_Str());
            if (found != nameToBone.end()) {
                unskinnedBone = found->second;
                out.skeleton.bones[static_cast<std::size_t>(unskinnedBone)].inverseBind =
                    anim::Mat4(1.0f);
            }
        }
        for (unsigned i = 0; i < mesh->mNumBones; ++i) {
            aiBone* bone = mesh->mBones[i];
            const std::string name = bone->mName.C_Str();
            auto found = nameToBone.find(name);
            if (found == nameToBone.end()) {
                const int index = out.skeleton.addBone(
                    anim::hashString(name), 0, anim::Transform{});
                if (index < 0) {
                    boneRemap[i] = 0;
                    continue;
                }
                out.boneNames.push_back(name);
                nameToBone.emplace(name, index);
                found = nameToBone.find(name);
            }
            const int index = found->second;
            boneRemap[i] = index;
            out.skeleton.bones[static_cast<std::size_t>(index)].inverseBind =
                mirror * toGlm(bone->mOffsetMatrix) * mirror;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            SkinVertex vertex{};
            const aiVector3D& position = mesh->mVertices[v];
            vertex.position[0] = -position.x;
            vertex.position[1] = position.y;
            vertex.position[2] = position.z;

            if (mesh->mNormals != nullptr) {
                const aiVector3D& normal = mesh->mNormals[v];
                vertex.normal[0] = -normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
            } else {
                vertex.normal[1] = 1.0f;
            }

            struct Influence {
                int bone;
                float weight;
            };
            std::array<Influence, 4> influences{};
            int influenceCount = 0;
            float total = 0.0f;
            for (unsigned b = 0; b < mesh->mNumBones; ++b) {
                aiBone* bone = mesh->mBones[b];
                for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                    const aiVertexWeight& weight = bone->mWeights[w];
                    if (weight.mVertexId != v || weight.mWeight <= 0.0f) continue;

                    Influence influence{boneRemap[b], weight.mWeight};
                    if (influenceCount < 4) {
                        influences[static_cast<std::size_t>(influenceCount++)] = influence;
                    } else {
                        auto smallest = std::min_element(
                            influences.begin(), influences.end(),
                            [](const Influence& a, const Influence& c) {
                                return a.weight < c.weight;
                            });
                        if (smallest != influences.end() && smallest->weight < influence.weight) {
                            *smallest = influence;
                        }
                    }
                }
            }
            for (int i = 0; i < influenceCount; ++i) total += influences[i].weight;

            if (total <= 0.0f) {
                vertex.indices[0] = static_cast<uint8_t>(std::max(unskinnedBone, 0));
                vertex.weights[0] = 255;
            } else {
                for (int i = 0; i < influenceCount; ++i) {
                    const int bone = std::clamp(influences[i].bone, 0, 191);
                    vertex.indices[i] = static_cast<uint8_t>(bone);
                    vertex.weights[i] =
                        static_cast<uint8_t>(std::clamp(influences[i].weight / total, 0.0f, 1.0f) *
                                             255.0f);
                }
            }
            out.vertices.push_back(vertex);
        }

        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned i = face.mNumIndices; i > 0; --i) {
                out.indices.push_back(baseVertex + face.mIndices[i - 1]);
            }
        }
    }

    if (out.vertices.empty() || out.indices.empty()) {
        error = "no skinned mesh data";
        return false;
    }

    if (!metaPath.empty()) {
        anim::converter::FbxHumanMap humanMap;
        std::string metaError;
        if (!anim::converter::parseFbxMetaFile(metaPath, humanMap, metaError)) {
            return true;
        }

        std::array<int32_t, anim::kHumanBoneCount> boneMap{};
        boneMap.fill(-1);
        for (const auto& entry : humanMap.entries) {
            const anim::HumanBone bone = anim::humanBoneFromName(entry.second);
            if (bone == anim::HumanBone::Count) continue;
            const auto found = nameToBone.find(entry.first);
            if (found != nameToBone.end()) {
                boneMap[static_cast<std::size_t>(anim::humanBoneIndex(bone))] = found->second;
            }
        }
        out.avatar = anim::createHumanoidAvatar(out.skeleton, boneMap);
        out.hasAvatar = true;
    }

    return true;
}

bool exportSkeletonJson(const Model& model, const std::string& path, std::string& error) {
    if (model.boneNames.size() != model.skeleton.bones.size()) {
        error = "bone name table does not match skeleton";
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot open " + path;
        return false;
    }

    out << "{\n  \"bones\": [\n";
    for (std::size_t i = 0; i < model.skeleton.bones.size(); ++i) {
        const anim::Bone& bone = model.skeleton.bones[i];
        out << "    {\"name\": \"" << model.boneNames[i] << "\", \"parent\": " << bone.parent
            << ", \"translation\": [" << bone.restLocal.translation.x << ", "
            << bone.restLocal.translation.y << ", " << bone.restLocal.translation.z
            << "], \"rotation\": [" << bone.restLocal.rotation.x << ", "
            << bone.restLocal.rotation.y << ", " << bone.restLocal.rotation.z << ", "
            << bone.restLocal.rotation.w << "], \"scale\": [" << bone.restLocal.scale.x << ", "
            << bone.restLocal.scale.y << ", " << bone.restLocal.scale.z << "]}";
        if (i + 1 < model.skeleton.bones.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    if (!out) {
        error = "write failed";
        return false;
    }
    return true;
}

}  // namespace demo
