#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "anim/avatar/HumanoidAvatar.h"
#include "anim/skeleton/Skeleton.h"

namespace demo {

struct SkinVertex {
    float position[3];
    float normal[3];
    uint8_t indices[4];
    uint8_t weights[4];
};

struct Model {
    anim::Skeleton skeleton;
    std::vector<std::string> boneNames;  // 与 skeleton.bones 一一对应（用于导出 JSON）
    std::vector<SkinVertex> vertices;
    std::vector<uint32_t> indices;
    anim::HumanoidAvatar avatar;
    bool hasAvatar = false;
};

// 导出 converter 可读的 skeleton.json（rest TRS + parent）。
bool exportSkeletonJson(const Model& model, const std::string& path, std::string& error);

// 用 assimp 读取 FBX：所有节点都进入 Skeleton（保持父子局部 TRS），
// 网格顶点保留最多 4 个骨骼索引/权重。metaPath 非空时按 Unity
// humanDescription.human 建立 HumanoidAvatar。
bool loadFbx(const std::string& fbxPath, const std::string& metaPath, Model& out,
             std::string& error);

}  // namespace demo
