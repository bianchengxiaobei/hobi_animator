#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "anim/clip/Curve.h"
#include "anim/core/Hash.h"
#include "anim/core/Math.h"

// Clip 类型定义。P0 只定义数据结构，采样在 P1/P4 实现。
//
// 文档：docs/03-动画数据与曲线.md §3、§5
namespace anim {

enum class ClipType : uint8_t {
    BoneLocal = 0,
    HumanoidMuscle = 1,
};

struct EventMarker {
    float normalizedTime = 0.0f;
    StringId name = kInvalidStringId;
};

// 非骨骼的命名浮点通道（对应 Unity 动画流里的属性曲线，如 "FootFall"）。
// path 为空时用 kInvalidStringId；attribute 是完整属性名（如 "FootFall"）。
// 见 Utilities/Animation Jobs/AnimatedProperty.cs 与 docs/10 §8。
struct PropertyChannel {
    StringId path = kInvalidStringId;
    StringId attribute = kInvalidStringId;
    Curve curve;
};

struct Clip {
    ClipType type = ClipType::BoneLocal;
    float length = 0.0f;
    bool looping = false;
    float averageSpeed = 0.0f;  // 对应 AnimationClip.averageSpeed
    std::vector<EventMarker> markers;
    std::vector<PropertyChannel> properties;

    virtual ~Clip() = default;

    const PropertyChannel* findProperty(StringId path, StringId attribute) const noexcept {
        for (const PropertyChannel& property : properties) {
            if (property.path == path && property.attribute == attribute) return &property;
        }
        return nullptr;
    }

    const PropertyChannel* findProperty(std::string_view path,
                                        std::string_view attribute) const noexcept {
        const StringId pathId = path.empty() ? kInvalidStringId : hashString(path);
        return findProperty(pathId, hashString(attribute));
    }
};


// 通用（generic）骨骼动画：path + m_LocalPosition/Rotation/Scale 通道。
// 见 Examples/Art/Spider Bot/SpiderBot-MoveUp.anim 的 m_Local* 通道。
struct BoneChannel {
    int32_t boneIndex = -1;   // 目标骨架中的骨骼索引（转换期由 path 绑定）
    Curve position[3];        // x, y, z
    Curve rotation[4];        // x, y, z, w
    Curve scale[3];           // x, y, z
    uint8_t hasPosition : 1;
    uint8_t hasRotation : 1;
    uint8_t hasScale : 1;
    uint8_t reserved : 5;

    BoneChannel() : hasPosition(0), hasRotation(0), hasScale(0), reserved(0) {}
};

struct BoneLocalClip final : Clip {
    std::vector<BoneChannel> channels;

    BoneLocalClip() { type = ClipType::BoneLocal; }
};

// 人形肌肉空间：95 个肌肉 DOF + RootT/RootQ + 手脚 IK goal。
// 见 Humanoid-Walk.anim（RootT.x、Spine Front-Back、LeftHand.Thumb.1 Stretched）。
struct HumanoidClip final : Clip {
    std::vector<Curve> muscles;  // 索引 = MuscleId（见 05）

    Curve rootT[3];  // RootT.x/y/z
    Curve rootQ[4];  // RootQ.x/y/z/w
    bool hasRootT = false;
    bool hasRootQ = false;

    Curve leftFootT[3];
    Curve rightFootT[3];
    Curve leftFootQ[4];
    Curve rightFootQ[4];
    Curve leftHandT[3];
    Curve rightHandT[3];
    Curve leftHandQ[4];
    Curve rightHandQ[4];
    bool hasFootT = false;
    bool hasFootQ = false;
    bool hasHandT = false;
    bool hasHandQ = false;

    HumanoidClip() { type = ClipType::HumanoidMuscle; }
};

// 采样入口（P1/P4 实现）。
struct Pose;
class Skeleton;
struct HumanoidAvatar;

void sampleClip(const Clip& clip, float time, const Skeleton& skeleton,
                const HumanoidAvatar* avatar, Pose& out);

}  // namespace anim
