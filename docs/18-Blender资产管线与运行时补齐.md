# 18 · Blender 资产管线与运行时补齐规划

> 状态：P8.1-P8.5d 已完成；P8.6 的文档、命令与合成资产验收已完成，真实 Blender 资产视觉验收待外部文件；P9 的 C++ 运行时跨骨架 clip 路径已实现，真实不同拓扑资产验收仍可选。本文定义“先做什么、按什么顺序做、每步如何验收”。
> 目标读者：自研 C++ 引擎的运行时/工具链开发者。
> 范围基准：[17-功能范围与验收](17-功能范围与验收.md)；本文只补充 **Blender → C++ 资产管线** 与必要的运行时门面。

## 1. 结论先行

`AnimancerCpp` 的运行时主链路已经可用：Clip、Layer、Fade、Transition、Event、Mixer、FSM、Root Motion、Jobs、Humanoid、bgfx 调色板都有实现和测试。当前真正缺的不是再移植一批 Animancer 类，而是：

1. **Blender 资产入口**：现在只有 Unity `.anim`（YAML）转 `.ab`，没有 Blender/glTF 路径；
2. **动画配置资产**：`ClipTransition` 只有运行时结构，没有可从磁盘读取的 fade/speed/loop/events 配置；
3. **事件接线**：`.ab` 的 `EventMarker` 不会自动变成 `AnimancerState` 的 `EventSequence`；
4. **生产级骨架加载**：sample 的 `FbxLoader` 不是通用加载器，`SkeletonLoader` 也没有 `inverseBind`；
5. **`AnimancerAnimator` 门面**：P8.5 已补齐原版常用的 `Play(clip, fade, mode)`、`Stop`、`IsPlaying`、`Layer` 转发入口。

因此本文的核心策略是：

```text
Blender -> glTF/GLB -> offline anim_pack -> .ab + skeleton.json + manifest.json
C++ runtime -> load assets -> AnimancerAnimator -> update -> palette
```

**先打通资产链路，再补门面便利性；不为了“像原版”继续复制 Unity 组件体系。**

## 2. 目标与非目标

### 2.1 目标

- Blender 制作的骨骼动画可以直接进入 C++ 运行时，不依赖 Unity 运行时或 Unity Editor。
- 同骨架（same rig）动画使用 `BoneLocalClip`，不要求 95 肌肉空间。
- 离线工具生成：
  - `*.skeleton.json`：骨名、parent、rest TRS、inverseBind、稳定骨序；
  - `*.ab`：每个 Blender Action 一个运行时 Clip；
  - `anim_manifest.json`：loop、fade、speed、start、rootBone、事件名。
- 运行时提供：
  - manifest 读取；
  - ClipTransition 构造；
  - 事件名到回调的绑定；
  - `AnimancerAnimator` 的常用便利方法。
- 保持现有 Unity `.anim -> .ab` 路径不回归，原有 195/195 测试保持通过。
- 所有新增工具和测试在 Windows + MSVC + C++20 下可构建、可验证。

### 2.2 非目标

以下内容不阻塞本规划：

- Unity Editor、Inspector、菜单、Editor 工具窗口；
- `RuntimeAnimatorController`、Timeline、`PlayableAssetState`；
- `HybridAnimancerComponent`、`NamedAnimancerComponent` 的完整复制；
- 跨骨架自动 Humanoid retarget（见 §9 的可选阶段）；
- 运行时解析 glTF/GLB；
- 动画压缩、量化、热重载、AssetBundle；
- 材质、贴图、渲染管线导入；
- Unity 2019 全骨逐值对拍。

同骨架 Blender 动画不需要 `HumanoidAvatar`；只有“不同骨架之间重定向”或“使用 Unity Humanoid 肌肉曲线”时才需要 Humanoid 管线。不要把同骨架动画强行转成 `HumanoidClip`。

## 3. 总体数据流

```text
Blender
  ├─ Armature / rest pose
  ├─ Skin inverseBindMatrices
  ├─ Actions（Walk / Run / Idle / Attack ...）
  └─ 可选 events.json（时间轴 Marker）
        │
        ▼
  character.glb
        │
        ▼
  tools/anim_pack（离线 C++ 工具）
  ├─ 建立 Skeleton（拓扑序 + inverseBind）
  ├─ 每个 Action -> BoneLocalClip
  ├─ 事件 -> Clip::markers / manifest
  └─ encodeClip() -> *.ab
        │
        ▼
  character.skeleton.json + *.ab + anim_manifest.json
        │
        ▼
  C++ runtime
  ├─ 加载 Skeleton
  ├─ 加载 manifest + decodeClip
  ├─ 构造 ClipTransition / EventSequence
  ├─ AnimancerAnimator::initialize
  └─ update(dt) -> palette
```

### 3.1 为什么用 glTF 作为中间格式

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| Blender → glTF/GLB → `anim_pack` | Blender 原生导出；skin/rest/inverseBind/动画一起带出；引擎通常已有 glTF 加载器 | 需要离线解析 glTF | **采用** |
| Blender Python → JSON → `anim_pack` | 不依赖 glTF 解析；可完全控制采样 | 要自己处理约束烘焙、rest/inverseBind、坐标转换 | 备选 |
| Blender → FBX → Assimp | sample 已用 Assimp | FBX 层级/单位/手性更容易出问题 | 不优先 |
| Blender Python 直接写 `.ab` | 看起来直接 | 复制 `.ab` 版本/布局，后续难维护 | **不采用** |

`.ab` 的写入只保留一份实现：`anim::encodeClip` / `anim::writeClip`（见 `include/anim/clip/ClipIO.h`、`src/clip/ClipIO.cpp`）。Blender 侧最多输出中间 JSON，不直接拼二进制。

### 3.2 同骨架与跨骨架

```text
同一套骨骼（Blender rig == runtime skeleton）
  -> BoneLocalClip
  -> 直接播放，精确、简单、无需 HumanoidAvatar

不同骨架
  -> 先在 Blender 里 retarget 到目标 rig
  -> 仍导出 BoneLocalClip
  -> 或者后续做 HumanoidAvatar/muscle-space retarget（单独阶段）
```

P8 只承诺第一条。跨骨架是 P9 可选项，除非实际项目明确需要。

## 4. 关键设计决策

### ADR-01：中间格式用 glTF 2.0

Blender 导出 `character.glb`；离线工具读 GLB。运行时只读 `.ab`，不引入 glTF 解析器。

### ADR-02：glTF 解析依赖只存在于离线工具

推荐使用 `cgltf`（MIT，单头文件）作为 `tools/anim_pack` 的私有依赖；不放进 `include/anim/**` 或核心库。

备选：如果项目已有 glTF 加载器，复用其离线导入模块；如果必须用 FBX，则把 `anim_pack` 的输入层替换为 Assimp，后续流程不变。

### ADR-03：同骨架优先 `BoneLocalClip`

P8 不生成 `HumanoidClip`。`HumanoidClip` 仅用于已有 Unity 肌肉曲线或后续跨骨架 retarget。

### ADR-04：复用 `.ab` 序列化

`anim_pack` 构造 `BoneLocalClip` 后调用：

```cpp
auto bytes = anim::encodeClip(clip, &error);
```

不实现第二套 `.ab` writer。`docs/03` §4 是格式的唯一说明。

### ADR-05：坐标转换在打包期完成

glTF 是右手系、Y-up；当前核心库不对世界手性做假设。约定：

- P8 默认按 glTF 坐标生成资产；
- 如果目标引擎是 Unity-like 的左手系，使用 `--coordinate mirrored` 在**打包期**一次性转换骨架、动画和 inverseBind；
- 运行时不再每帧转坐标；
- 坐标转换必须有“双次镜像 = 原值”的单元测试。

### ADR-06：事件以 manifest 为运行时来源

`.ab` 的 `EventMarker` 只有 `normalizedTime + StringId`，无法反查事件名，也不会自动生成 `EventSequence`。P8 的规则：

- `anim_manifest.json` 保存事件名和归一化时间；
- 运行时按事件名绑定回调；
- `.ab` 的 markers 只用于兼容/离线查看，不作为唯一事件来源；
- 同一 Clip 的 manifest 事件与 marker 不应重复触发。

### ADR-07：Transition 按角色持有

`ClipTransition::apply()` 会把 `&eventSequence` 交给 State；如果回调捕获角色 `this`，共享一个 Transition 会导致所有角色共用同一事件上下文。约定：

- 纯静态事件可共享；
- 角色相关事件使用每角色 `ClipTransition` 或 `UnShared<ClipTransition>` 拷贝；
- `ClipTransition`、`EventSequence`、`Clip`、`Skeleton`、`Avatar` 的生命周期必须覆盖 State/Animancer。

### ADR-08：核心库不引入 glTF/第三方 JSON 依赖

manifest 读取器放在 `integration` 层，使用仓库内最小的 JSON 读取器（已从 `SimpleJson` 提取为 `integration/Json.*`）；glTF 解析只存在于 `tools/anim_pack`。这样核心算法仍不拉入 cgltf/Assimp/外部 JSON 库。

## 5. 资产契约

### 5.1 `*.skeleton.json`

沿用现有格式（`tools/anim_converter/SkeletonLoader.h`），增加可选的 `inverseBind`：

```json
{
  "version": 1,
  "bones": [
    {
      "name": "Root",
      "parent": -1,
      "translation": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1],
      "inverseBind": [
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
      ]
    }
  ]
}
```

约束：

- `bones` 顺序就是运行时 `boneIndex`；
- parent 必须小于 child（拓扑序）；
- `rotation` 用 `[x, y, z, w]`；
- `inverseBind` 用列主序 4×4；
- `inverseBind` 缺失时，加载器可先根据 rest world 计算 `inverse(world)`，但 glTF 正式产物应写入 skin 的真实 inverseBind；
- 骨名必须唯一；
- 运行时 Skeleton 和 `.ab` 必须使用同一份骨架顺序。

### 5.2 `anim_manifest.json`

这是供运行时读取的资产清单，也是 `anim_pack` 的输入配置：

```json
{
  "version": 1,
  "skeleton": "character.skeleton.json",
  "coordinate": "gltf",
  "clips": [
    {
      "name": "Walk",
      "action": "Walk",
      "file": "Walk.ab",
      "loop": true,
      "fade": 0.2,
      "speed": 1.0,
      "normalizedStartTime": 0.0,
      "rootBone": "Hips",
      "events": [
        { "normalizedTime": 0.0, "name": "Footstep.Left" },
        { "normalizedTime": 0.5, "name": "Footstep.Right" }
      ]
    }
  ]
}
```

字段：

| 字段 | 必填 | 说明 |
|------|------|------|
| `version` | 是 | manifest 版本，当前 1 |
| `skeleton` | 是 | 骨架 JSON 路径，相对 manifest |
| `coordinate` | 否 | `gltf`（默认）或 `mirrored`；仅打包期使用 |
| `clips[].name` | 是 | 运行时 Clip 名/查找键 |
| `clips[].action` | 否 | glTF animation 名；默认同 `name` |
| `clips[].file` | 是 | 输出 `.ab` 文件名 |
| `clips[].loop` | 否 | 默认 false |
| `clips[].fade` | 否 | 默认 0.25 秒 |
| `clips[].speed` | 否 | 默认 1.0 |
| `clips[].normalizedStartTime` | 否 | 省略时用默认起点 |
| `clips[].rootBone` | 否 | 根运动骨名；为空则使用骨架默认根骨（索引 -1） |
| `clips[].events` | 否 | 归一化时间 + 事件名 |

`anim_manifest.json` 被复制到输出目录后即可直接给运行时使用。`anim_pack` 负责校验路径、事件时间和 Clip 骨索引，但不修改作者填写的业务事件名。

### 5.3 `.ab` 内容

P8 产物统一为 `BoneLocalClip`：

- `clip.length` = 动作最大时间；
- `clip.looping` = manifest 的 `loop`；
- `clip.averageSpeed` 可选；根运动需要时由 packer 估算；
- 每个通道只写存在变化的 `position`/`rotation`/`scale`；
- 未出现的骨骼保持 Skeleton rest pose；
- `markers` 可选；manifest 事件是运行时权威；
- 属性通道（`PropertyChannel`）暂不由 Blender packer 生成。

`.ab` 格式和读写以 `docs/03` §4 与 `ClipIO.cpp` 为准。

### 5.4 运行时校验

加载时必须检查：

- `.ab` magic/version；
- `clip.type == BoneLocal`（P8 范围）；
- 每个 `boneIndex < skeleton.boneCount()`；
- `clip.length >= 0`、时间轴单调；
- manifest 的 `rootBone` 能解析成 index；
- 事件 `normalizedTime` 在 `[0,1]`；
- `fade >= 0`、`speed` 非零且有限；
- 同一 manifest 内 Clip 名唯一。

## 6. `anim_pack` 设计

### 6.1 CLI

```text
anim_pack --input character.glb
          --out-dir build/anim
          --manifest clips.json
          [--skeleton-out character.skeleton.json]
          [--coordinate gltf|mirrored]
          [--strip-action-prefix Armature|]
          [--events events.json]
```

约定：

- `--manifest` 是输入/输出共用的 manifest；`file` 字段指向 `--out-dir`；
- `--skeleton-out` 不指定时写 `<out-dir>/<input-stem>.skeleton.json`；
- `--events` 是 §8.3 的 Blender Marker sidecar；匹配的 Action 覆盖 manifest 事件；
- 输出目录不存在则创建；
- 成功返回 0，任何骨架/动画/校验错误返回非 0 并打印文件与字段。

已实现命令：

```powershell
cmake -S . -B build -DANIM_BUILD_ANIM_PACK=ON
cmake --build build --config Release --target anim_pack
build/tools/anim_pack/Release/anim_pack.exe `
  --input character.glb `
  --out-dir build/anim `
  --manifest clips.json `
  --coordinate mirrored
```

### 6.2 处理流程

1. 解析 GLB。
2. 找到 skin：
   - 读取 `skin.joints`；
   - 读取 `inverseBindMatrices`；
   - 建立 `glTF node -> boneIndex` 映射；
   - 按 parent 拓扑排序，保证 `Skeleton::addBone` 的 parent-before-child 约束；
   - 从 joint 的 node TRS 读取 rest local；
   - 将非 joint 父链变换合并到根 joint 或显式报错（P8 选择“可合并的 Armature 变换合并，无法合并则报错”）。
3. 读取每个 animation：
   - 只处理 target 为 joint 的通道；
   - translation/rotation/scale 分别生成 3/4/3 条 `Curve`；
   - 处理 LINEAR / STEP / CUBICSPLINE；
   - 四元数做半球连续性修正；
   - `clip.length` 取所有通道最大时间；
   - `clip.looping` 从 manifest 取；
   - 事件写入 `Clip::markers`（可选）并保留 manifest。
4. 生成 `SkeletonAsset` 并写 `*.skeleton.json`。
5. 调用 `encodeClip` 写每个 `.ab`。
6. 若提供 `--events`，按 sidecar 的 Action frame range 覆盖 manifest 事件并写入 markers。
7. 将校验后的 manifest 复制/写到输出目录。
8. 打印摘要：骨架骨数、Clip 数、每个 Clip 的通道数/关键帧数/长度/事件数。

### 6.3 glTF 插值转换

| glTF | `.ab` |
|------|-------|
| `LINEAR` | 每个关键帧写入 `inSlope/outSlope`，按相邻时间差计算 |
| `STEP` | 左侧关键帧 `constant = true` |
| `CUBICSPLINE` | 读取 in/out tangent，映射为 `inSlope/outSlope` |
| 无 sampler 数据 | 报错，不静默生成空 Clip |

四元数：

- 逐关键帧检查 `dot(q[i], q[i-1])`；小于 0 时翻转整个四元数；
- 采样后由 `Curve` + `sampleClip` 归一化；
- 不要只翻转单个分量。

### 6.4 根运动

- manifest 的 `rootBone` 指定根骨骼；
- packer 不自动“去除”根运动；根骨骼通道原样写入；
- 运行时通过 `playable().setRootMotionRootBone(index)` 和 `consumeRootMotion()` 决定是否消费；
- 如果 Blender 的根运动在 Armature Object 上，先烘焙到 root bone；P8 packer 可对可识别的 object 动画报警，不静默忽略。

### 6.5 错误与确定性

必须报错：

- 没有 skin / 没有 joint；
- joint 的 parent 不是 joint 且无法合并；
- inverseBind 数量与 joints 不一致；
- animation 的 target node 不在 joint 映射中；
- manifest 的 action 不存在；
- 同一输出文件被多个 Clip 使用；
- 事件时间越界；
- sidecar frame 超出 `[frameStart, frameEnd]`、range 为空或 Action 未使用；
- 骨索引越界。

输出要求：

- 同一输入/参数重复运行产生相同字节（不写时间戳/随机顺序）；
- 骨架顺序稳定；
- manifest 字段顺序稳定；
- 警告和错误分离：警告打印但返回 0，错误返回非 0。

## 7. 运行时补齐

### 7.1 `AnimationAsset` POD

新增 `include/anim/integration/AnimationAssets.h`，只放数据结构和无 JSON 依赖的接口：

```cpp
namespace anim {

struct ClipEventInfo {
    float normalizedTime = 0.0f;
    std::string name;
};

struct ClipAssetInfo {
    std::string name;
    std::string action;
    std::string file;
    bool looping = false;
    float fadeDuration = 0.25f;
    float speed = 1.0f;
    float normalizedStartTime = std::numeric_limits<float>::quiet_NaN();
    std::string rootBone;
    std::vector<ClipEventInfo> events;
};

struct AnimationSet {
    int version = 1;
    std::string skeletonFile;
    std::string coordinate = "gltf";
    std::vector<ClipAssetInfo> clips;
    const ClipAssetInfo* find(std::string_view name) const noexcept;
    ClipAssetInfo* find(std::string_view name) noexcept;
};

using EventCallback = std::function<void()>;
using EventCallbackMap = std::unordered_map<std::string, EventCallback>;

}  // namespace anim
```

这些结构不持有 `Clip`/`Skeleton`，避免把资源生命周期混进配置层。

### 7.2 manifest 读取

已实现：

```cpp
bool loadAnimationManifest(std::string_view text, AnimationSet& out, std::string& error);
bool loadAnimationManifestFile(const std::string& path, AnimationSet& out, std::string& error);
```

- 已将 `tools/anim_converter/SimpleJson.*` 提取到 `include/anim/integration/Json.h` / `src/integration/Json.cpp`；
- 不引入第三方 JSON 依赖；
- 严格校验字段类型和必填项；
- `loadAnimationManifestFile` 将相对路径解析到 manifest 所在目录。

### 7.3 事件绑定

运行时流程（P8.4）：

```cpp
anim::AnimationSet manifest;
anim::loadAnimationManifestFile("anim_manifest.json", manifest, error);

anim::AnimationClipLibrary clips;
clips.load(manifest, error);                 // 共享：每个 .ab 只 decode 一次

anim::EventCallbackMap callbacks;
callbacks["Footstep.Left"] = [] { /* ... */ };
callbacks["Footstep.Right"] = [] { /* ... */ };

anim::AnimationTransitions transitions;      // 每角色一份
transitions.build(clips, callbacks, error);

anim::AnimancerAnimator animator;
animator.initialize(skeleton);
anim::AnimancerState* state = transitions.play(animator, "Walk", &error);
```

`AnimationClipLibrary` 持有 decode 后的 `Clip`，并用 manifest 的 `loop` 覆盖 `.ab`
内的循环标记；`AnimationTransitions` 为每个 manifest Clip 构造独立的
`ClipTransition`/`EventSequence`，并从回调表按事件名绑定。库里没有注册回调的事件
会被跳过，不会生成空回调。

注意：

- `EventSequence::add` 的回调无参数；角色相关逻辑通过每角色 Transition 或 lambda 捕获 `this`；
- 共享 `Clip` 可以，但不要让两个角色共享同一份 `AnimationTransitions`；每角色构建一份，
  事件上下文才互不串；
- looping Clip 的事件必须在 `[0, 1)`；`AnimationTransitions::build` 会拒绝 `>= 1` 的事件；
- `UnShared<ClipTransition>` 仍可用于手工配置的 Transition；manifest 路径直接使用
  `AnimationTransitions`，它已经持有每角色副本；
- `AnimationClipLibrary` 必须晚于所有 `AnimancerAnimator` 析构，`AnimationTransitions`
  必须晚于使用其事件的 State/Animancer 析构；
- `.ab` 的 markers 不自动触发；manifest 事件是唯一入口（除非后续补 marker 自动接线）。

### 7.4 `AnimancerAnimator` 门面

在不破坏现有 API 的前提下补：

```cpp
AnimancerState* play(const Clip& clip, float fadeDuration, FadeMode mode);
AnimancerState* play(ITransition& transition, float fadeDuration, FadeMode mode);
void stop();
bool isPlaying() const;
bool isPlaying(const Clip& clip) const;
AnimancerLayer* layer(int index);
```

实现只是转发到 `playable()` / `layers()`；不复制核心逻辑。P8.5 已在
`AnimancerAnimator.h` / `.cpp` 中完成，clip 与 transition 的 fade 重载都直接调用 layer 0。

### 7.5 资源生命周期

推荐结构：

```text
Skeleton* / HumanoidAvatar*  （共享，外部生命周期）
AnimationClipLibrary          （共享：每个角色复用 decode 后的 Clip）
AnimationTransitions          （每角色一份；持有自己的 EventSequence）
AnimancerAnimator
```

必须保证：

- `Skeleton`/`Avatar` 在 `AnimancerAnimator` 之前析构；
- `AnimationClipLibrary` 在使用其 Clip 的 State/Animancer 之后析构；
- `AnimationTransitions` 在引用其 EventSequence 的 State/Animancer 之后析构；
- 播放期间不要重新 `load`/`build`，否则旧指针会失效；
- `Clip`/`EventSequence` 在 State 生命周期内有效；不要用局部 `ClipTransition`。

### 7.6 根运动

```cpp
// AnimationTransitions::play 会读取当前 Clip 的 manifest rootBone，
// 解析成 Skeleton 索引并调用 setRootMotionRootBone（空字符串则设为 -1，使用骨架默认根）。
transitions.play(animator, "Walk", &error);

animator.playable().setRootMotionTarget(&target); // 由调用方选择目标；库不自动挂
animator.update(dt);
const anim::RootMotionDelta delta = animator.consumeRootMotion();
// 由游戏逻辑决定是否应用到实体
```

可用 `animator.playable().rootMotionRootBone()` 检查当前绑定的骨骼索引。当前语义是
Hips-root；可选 `HumanoidRootMode::MassCenter` 已提供，默认仍为 Hips-root（见 `docs/17` §3）。

## 8. Blender 工作流

以下步骤以 Blender 3.x/4.x 的 glTF 2.0 导出器为准。目标是同骨架动画，不经过
Unity，也不使用 Humanoid 肌肉曲线。

### 8.1 导出前检查

1. **只保留一个 Armature**，所有可播放骨骼都在这个 Armature 下；骨骼命名在项目内稳定。
2. **应用对象变换**：`Object > Apply > All Transforms`；避免负缩放和非均匀缩放。
3. **烘焙约束**：如果有 IK、Copy Rotation 等约束，先 bake 到骨骼 Action，再导出。
4. **Action 命名**：每个 Action 对应一个 Clip；Action 名最终成为 manifest 的
   `action`。例如 Blender Action 为 `Armature|Walk`，manifest 可以写
   `"action": "Armature|Walk"`，也可以给 `anim_pack` 传
   `--strip-action-prefix "Armature|"` 后写 `"action": "Walk"`。
5. **根运动**：确认位移在 root bone（推荐）还是 Armature Object。若在 Object 上，
   先在 Blender 中烘焙到 root bone；P8 packer 不会把 Object 动画静默转换成骨骼动画。
6. **事件**：在 Action 的 Pose Markers 上添加事件，或在场景 Timeline Markers 上添加；
   见 §8.3。Marker 名称就是运行时事件名。
7. **坐标系**：确认目标引擎坐标。需要镜像时，`anim_pack` 传
   `--coordinate mirrored`；不要在 Blender 里额外手动旋转根骨。

### 8.2 glTF 导出设置

在 Blender 中执行 `File > Export > glTF 2.0 (.glb/.gltf)`，使用以下设置：

- Format：`glTF Binary (.glb)`
- Include：至少包含 Armature 和 mesh；建议只导出目标 Armature/角色相关的对象
- Animation Mode：`Actions`（推荐）或 `NLA Tracks`（二选一，不要混用）
- Sampling Rate：`1`（逐帧）
- Always Sample Animations：开
- Optimize Animation Size：关
- Export Skins：开
- Export Deformations：开
- Y Up：开（glTF 标准）
- Apply Modifiers：按资产需要开；带形态键时先确认导出结果

如果使用 NLA，确保每个 strip 都指向一个明确的 Action，且没有空 Action；NLA
strip 的时间偏移会让事件帧与 Action 帧范围不一致，优先使用 `Actions` 模式。

导出后应得到：

```text
character.glb        # skin + rest/inverseBind + 每个 Action 的动画
character.blend      # 保留在美术侧，不提交到运行时资源目录
```

### 8.3 事件导出

glTF 不保存 Blender Marker，因此 P8 使用 sidecar。推荐在 **Action 的 Pose Markers**
上放事件；Pose Marker 属于单个 Action，不会把同一事件错误地复制给所有 Action。

用 Blender 后台模式运行可选脚本（不需要打开 UI；CI 不需要安装 Blender）：

```powershell
blender -b character.blend --python tools/blender/export_events.py -- `
  --output build/anim/events.json `
  --strip-action-prefix "Armature|"
```

脚本只读取 Action 的 `frame_range` 和 Marker，不导出动画。sidecar 格式：

```json
{
  "version": 1,
  "fps": 24,
  "markerSource": "pose",
  "actions": {
    "Walk": {
      "frameStart": 0,
      "frameEnd": 24,
      "events": [
        {"frame": 0, "normalizedTime": 0.0, "name": "Footstep.Left"},
        {"frame": 12, "normalizedTime": 0.5, "name": "Footstep.Right"}
      ]
    }
  }
}
```

`anim_pack` 会按下面的公式把 `frame` 转换为运行时事件时间：

```text
normalizedTime = (frame - frameStart) / (frameEnd - frameStart)
```

`normalizedTime` 必须落在 `[0, 1]`。脚本还会把换算结果写入每个事件供人工检查；
`anim_pack` 以 `frame + frameStart/frameEnd` 重新计算，避免手写值漂移。`fps` 用于记录
Blender 导出环境，不改变 glTF 采样结果。

脚本选项：

- `--action Walk`：只导出指定 Action，可重复；
- `--all-actions`：连没有 Marker 的 Action 也写入 frame range；
- `--marker-source pose|scene`：默认 `pose`；`scene` 会把场景 Timeline Marker
  应用到每个选中的 Action（通常只适合单 Action 场景）；
- `--strip-prefix "Armature|"`：与 `anim_pack --strip-action-prefix` 保持一致；
- `--fps 30`：覆盖场景 FPS；
- `--self-test`：不需要 Blender，验证帧到归一化时间的换算。

将 sidecar 交给 packer：

```powershell
build/tools/anim_pack/Release/anim_pack.exe `
  --input character.glb `
  --out-dir build/anim `
  --manifest anim_manifest.json `
  --events build/anim/events.json `
  --strip-action-prefix "Armature|" `
  --coordinate mirrored
```

规则：

- sidecar 中出现的 Action 会覆盖 manifest 中同一个 `action` 的 `events`；
- sidecar 中没有出现的 Action 保留 manifest 原有事件；
- sidecar 出现 manifest 未使用的 Action 时直接报错，防止拼写错误；
- 同一事件名可以出现多次；运行时按时间顺序触发；
- `.ab` 的 markers 仍是兼容/离线信息，运行时事件以 manifest 为准；
- 事件排序、循环触发和 end event 语义按原项目
  `Assets/Plugins/Animancer/Internal/Core/AnimancerEvent.Sequence.cs`；P8.4 的
  `AnimationTransitions` 已接线普通事件，sidecar 只负责提供事件数据。

### 8.4 完整命令与运行时播放

```powershell
# 0. 构建（packer 与 headless demo 一起）
cmake -S . -B build -DANIM_BUILD_ANIM_PACK=ON
cmake --build build --config Release --target anim_pack anim_pack_demo

# 1. Blender 导出 character.glb；准备 anim_manifest.json
# 2. 导出事件 sidecar
blender -b character.blend --python tools/blender/export_events.py -- `
  --output build/anim/events.json --strip-action-prefix "Armature|"

# 3. 打包
build/tools/anim_pack/Release/anim_pack.exe `
  --input character.glb `
  --out-dir build/anim `
  --manifest anim_manifest.json `
  --events build/anim/events.json `
  --coordinate mirrored

# 4. 不依赖渲染的播放验收（manifest 模式）
build/tools/anim_pack/Release/anim_pack_demo.exe `
  --manifest build/anim/anim_manifest.json `
  --clip-name Walk --seconds 2 --samples 8

# 或只验证单个打包后的 Clip
build/tools/anim_pack/Release/anim_pack_demo.exe `
  --skeleton build/anim/character.skeleton.json `
  --clip build/anim/Walk.ab `
  --seconds 2 --samples 8
```

`anim_pack_demo` 使用与运行时相同的 `AnimancerAnimator` / `ClipTransition`，会打印
每个采样点的 root local translation 和通过 `RootMotionToTransform` 消费后的角色位移。
manifest 模式还会通过 `AnimationTransitions` 绑定事件并打印触发的事件名，同时按 manifest
的 `rootBone` 设置根运动骨骼；直接 Clip 模式仍使用索引 0 作为演示根骨。
真实游戏应使用 `PlayableGraph`/`AnimancerPlayable` 的常规循环，不需要 demo 代码。

### 8.5 示例资产与验收边界

本仓库**不提交真实 Blender 二进制资产**（`.blend`/由 Blender 导出的 `.glb`），
因为二进制会随 Blender 版本、导出设置和许可证变化；美术侧资产应放在外部资源目录：
`tests/fixtures/anim_pack/simple_rig.glb` 是用于契约/插值测试的合成夹具，不是 Blender
导出物。真实资产的外部验收步骤：

1. 用 §8.1–§8.4 的命令导出 `character.glb` 和 `events.json`；
2. 用 `anim_pack` 生成 skeleton、`.ab`、manifest；
3. 用 `anim_pack_demo` 或游戏运行时播放 Walk/Run/Idle；
4. 检查脚底接触、循环首尾、根运动方向和 Marker 时间点；
5. 用同一 Blender rig 导出的模型做 bgfx 截图（模型的骨骼顺序必须与
   `character.skeleton.json` 一致；当前 `bgfx_demo` 不做自动重映射）。

视觉验收命令模板：

```powershell
# 先完成 §8.4 的 anim_pack；下面假设 character.fbx 是同一 Blender rig 的模型
cmake --build build --config Release --target bgfx_demo
build/samples/bgfx_demo/Release/bgfx_demo.exe `
  --model character.fbx `
  --clip build/anim/Walk.ab `
  --shaders build/samples/bgfx_demo/Release/shaders `
  --frames 65 --fixed-dt 0.0333333 `
  --camera-yaw 25 --screenshot build/blender_walk --screenshot-frame 30
```

若模型只能用 glTF 加载，需由引擎侧把 `character.glb` 的蒙皮/骨架接到相同
`skeleton.json` 顺序；这是引擎适配，不是 P8 packer 的运行时职责。

P8.3 已在合成 GLB 上跑通 `anim_pack + AnimancerAnimator` 播放；真实 Blender 资产的
视觉验收仍需美术提供文件后执行，不能由合成夹具替代。

## 9. 分阶段实施

### P8.1 契约与测试夹具

任务：

- [x] 新增 `include/anim/integration/AnimationAssets.h`。
- [x] 提取/复用 `SimpleJson`，实现 `AnimationSet` manifest 读取。
- [x] 扩展 `skeleton.json` 读取器支持可选 `inverseBind`。
- [x] 新增 `tests/fixtures/anim_pack/`：小型合成 glTF（2~3 骨、2 个动作、LINEAR/STEP/CUBICSPLINE 各一）。
- [x] 新增 manifest 解析/校验单测。
- [x] 更新 `docs/11`、`docs/12` 的资产路径说明。

DoD：

- 原有 195/195 测试仍通过；P8.1 完成时主构建 201/201，P8.2 完成后为 202/202；
- 新 manifest/骨架测试通过；
- `inverseBind` 缺失时 fallback、存在时优先；
- 骨架 parent 顺序错误有明确错误信息。

### P8.2 `anim_pack` 核心

任务：

- [x] 新增 `tools/anim_pack/`，接入 cgltf（或复用项目已有 glTF 解析器）。
- [x] 实现 GLB skeleton 提取、拓扑排序、inverseBind、rest local。
- [x] 实现 animation channel → `BoneLocalClip`。
- [x] 实现 LINEAR/STEP/CUBICSPLINE、四元数连续性。
- [x] 实现 `--coordinate gltf|mirrored` 和一次性转换测试。
- [x] 调用 `encodeClip` 写 `.ab`；写 `skeleton.json` / manifest。
- [x] CMake option：`ANIM_BUILD_ANIM_PACK`，不影响默认构建。

DoD：

- 合成 glTF 可生成 `.ab`；
- `decodeClip` 读回后，在多个时间点采样与期望值一致；
- 骨序/逆绑定/根运动正确；
- 重复运行输出字节稳定；
- 各种错误输入有非 0 返回和可定位错误。

已实现：`cgltf` 1.14（MIT，单头文件）放在 `tools/anim_pack/third_party/`，只被离线工具链接；
非 joint 父链的静态变换会合并进 joint rest local，并烘焙到该 joint 的动画 T/R/S 与切线，
无法表示为 local TRS 时明确报错；`--coordinate mirrored` 同时镜像 rest local、inverseBind、
动画 translation/rotation 及切线；CUBICSPLINE 四元数切线按 `(x,y,z,w) -> (-x,-y,z,w)` 镜像。
同时修正 `Coordinate.h` 的 `flipZ(Quat)` 为 `(w,-x,-y,z)`，新增带旋转的世界链回归测试。
主构建保持 202/202，`ANIM_BUILD_ANIM_PACK=ON` 时 209/209（P8.2 完成时）。

### P8.3 Blender 工作流文档与脚本

任务：

- [x] 按 §8 写完整 Blender 导出步骤。
- [x] 增加可选 `tools/blender/export_events.py`（只导出事件/帧范围，不导出动画）。
- [x] 增加一个真实 Blender 导出的最小示例资产（或明确说明用外部资产验证，未提交二进制）。
- [x] 用示例资产跑通 `anim_pack` + 运行时播放。

DoD：

- 文档从“Blender 点击哪些选项”到“生成哪些文件”可直接照做；
- 事件帧到 normalizedTime 的换算有示例；
- 示例 glTF 资产的骨架/动画能在 demo 中播放；真实 Blender 资产待外部验收（见 §8.5）。

已实现：§8 已补齐 Blender 设置、Action/Marker 规则、sidecar 格式和完整命令；
`tools/blender/export_events.py` 支持 `pose/scene` Marker、Action 过滤、前缀剥离、
FPS 覆盖和不依赖 Blender 的 `--self-test`；`anim_pack --events` 读取
`frameStart/frameEnd/events`，按 Action 覆盖 manifest 事件，未知 Action 直接报错；
新增 `samples/anim_pack_demo`（构建目标 `anim_pack_demo`）和
`AnimPack.RuntimePlaysPackedClip`，已在 `simple_rig.glb` 上跑通
`anim_pack -> AnimancerAnimator -> palette`。真实 Blender 二进制未提交，按 §8.5 由外部
资产验收；P8.3 完成时 `ANIM_BUILD_ANIM_PACK=ON` 测试为 213/213。

### P8.4 运行时 manifest + 事件

任务：

- [x] 实现 `loadAnimationManifest*`。
- [x] 从 `ClipAssetInfo` 构造 `ClipTransition`。
- [x] 实现事件名 → 回调映射。
- [x] 处理每角色 Transition 生命周期和 `UnShared`。
- [x] 绑定 `rootBone` 到根运动。
- [x] 新增运行时示例（扩展 `samples/anim_pack_demo` 的 `--manifest` 模式）。

实现：

- `AnimationClipLibrary`：按 manifest 批量读取 `.ab`，共享 `Clip`，并用 `loop` 覆盖
  `.ab` 内的循环标记；
- `AnimationTransitions`：每角色持有独立的 `ClipTransition`/`EventSequence`，按事件名
  绑定 `EventCallbackMap`，`play()` 自动解析并绑定 `rootBone`；
- `AnimancerPlayable::rootMotionRootBone()` 提供绑定结果查询；
- `anim_pack_demo --manifest ... [--clip-name Walk]` 复用同一套运行时路径，事件触发时打印。

DoD：

- [x] manifest 中 loop/fade/speed/start 生效；
- [x] 事件每循环触发一次，不在同一帧重复；
- [x] 两个角色使用同一 manifest 时事件上下文互不串；
- [x] 根运动由调用方消费，不自动改 Transform。

### P8.5 `AnimancerAnimator` 门面

任务：

- [x] 添加 §7.4 的转发方法。
- [x] 补 fade/stop/isPlaying 单测。
- [x] 保持现有 `play(clip)` 行为不变。

实现（对齐原 `Assets/Plugins/Animancer/AnimancerComponent.cs:470-644` 的
Play/Stop/IsPlaying 门面，但不复制组件体系）：

- `AnimancerAnimator::play(clip, fade, mode)` / `play(transition, fade, mode)` 直接转发到
  `layers().getLayer(0)`，不复制 fade 逻辑；
- `stop()` 转发到 `AnimancerPlayable::stop()`，遍历所有层；
- `isPlaying()` / `isPlaying(clip)` 转发到 Playable；未初始化时安全返回 `false`；
- `layer(index)` 返回 `layers().getLayer(index)`，越界或未初始化返回 `nullptr`；
- 新增 4 个 `Animancer` 测试：clip fade、transition fade、跨层 stop、未初始化安全。

DoD：

- [x] `play(clip, fade, mode)` 与 `layers()[0]->play` 行为一致；
- [x] `stop()` 停止所有层；
- [x] `isPlaying()` 与 Playable 状态一致；
- [x] 现有测试不回归。

### P8.6 验收与文档

任务：

- [x] 更新 `README`、`docs/11`、`docs/12`、`docs/17` 的实现状态。
- [x] 增加 `anim_pack` 命令示例。
- [x] 增加“Blender 同骨架 → C++”端到端验收命令模板。
- [x] 记录新增测试数量与基线。

实现：

- `README`、`docs/11`、`docs/12`、`docs/17` 已更新到 P8.5/P8.6 口径；
- 合成夹具 `tests/fixtures/anim_pack/simple_rig.glb` 的完整命令可执行：
  `anim_pack`（含 events）→ `anim_pack_demo` 直接模式 → manifest 模式；
- §8.5/§10.2 增加真实 Blender 同骨架资产的命令模板：`character.glb` +
  `events.json` → `anim_pack` → `anim_pack_demo` / `bgfx_demo` 截图；
- P8.6 完成时基线：主构建 **213/213**、`ANIM_BUILD_ANIM_PACK=ON` **224/224**、
  EnTT **214/214**（Debug/Release）；P9 运行时核心追加 1 个测试后为 214/225/215。

DoD：

- [x] 合成夹具的文档命令全部可执行；
- [x] `ctest` 全绿（主构建、pack、EnTT）；
- [ ] 至少一个真实 Blender/glTF 资产完成视觉验收——仓库未提交真实 `.blend`/Blender
  导出的 `.glb`，需外部资产；合成夹具只能验证数据/运行时链路，不能替代视觉签字；
- [x] 现有 Unity `.anim` 转换路径不受影响（原有测试全绿）。

### P9（可选）：跨骨架 Humanoid

只有在同骨架方案不能满足需求时再做。当前已实现 C++ 运行时核心路径：

- `RetargetClipNode`：采样源 `BoneLocalClip`，经 P4 已按 Unity `RetargetFrom` 语义
  实现的 `retargetHumanoidLocalPose` 输出目标骨架 Pose；
- `RetargetClipState` / `RetargetClipTransition`：接入 `AnimancerLayer`/Playable 图，
  支持 fade/speed/start/events 和状态生命周期；
- 源 `Skeleton`/`HumanoidAvatar`、目标 `HumanoidAvatar` 和 Transition（含事件序列）
  由调用方保证生命周期；目标骨架来自 `AnimancerPlayable::skeleton()`；
- 仅支持源 `ClipType::BoneLocal`；源 Humanoid 肌肉 Clip 仍走现有
  `HumanoidClip + target avatar` 路径；
- 新增 `Animancer.RetargetClipPlaysOnDifferentSkeleton` 测试，验证源 1m Hips
  到目标 2m Hips 的根平移缩放。

仍需外部资产的部分：

- Blender 侧先 retarget 到目标 rig，或美术提供不同拓扑的第二骨架；
- T-Pose/axes 标定、不同骨骼数量/缺失骨的真实视觉验收；
- 不阻塞 P8 发布。

## 10. 测试与验收

### 10.1 单元测试

- manifest：必填字段、默认值、事件时间、重复 Clip 名；
- skeleton：topology、inverseBind 缺失/存在、骨名唯一；
- glTF：joint 排序、非 joint 父链、translation/rotation/scale 通道；
- 插值：LINEAR/STEP/CUBICSPLINE 与期望值；
- 四元数：符号连续性；
- `.ab`：`encodeClip` → `decodeClip` → 采样；
- 事件 sidecar：frame range、越界、未知 Action、manifest 覆盖；
- 坐标：`--coordinate mirrored` 双次镜像恢复原值；
- 根运动：root bone 通道；
- 运行时播放：打包后的 skeleton + `.ab` 经 `AnimancerAnimator` 播放并采样；
- 门面：fade/stop/isPlaying。

### 10.2 集成验收

```powershell
cmake -S . -B build_pack -DANIM_BUILD_ANIM_PACK=ON
cmake --build build_pack --config Release --target anim_pack anim_pack_demo
ctest --test-dir build_pack -C Release --output-on-failure
build_pack/tools/anim_pack/Release/anim_pack.exe `
  --input tests/fixtures/anim_pack/simple_rig.glb `
  --out-dir build_pack/anim_out `
  --manifest tests/fixtures/anim_pack/simple_rig_manifest.json `
  --events tests/fixtures/anim_pack/simple_rig_events.json
build_pack/tools/anim_pack/Release/anim_pack_demo.exe `
  --skeleton build_pack/anim_out/simple_rig.skeleton.json `
  --clip build_pack/anim_out/Walk.ab --seconds 1 --samples 4
build_pack/tools/anim_pack/Release/anim_pack_demo.exe `
  --manifest build_pack/anim_out/simple_rig_manifest.json `
  --clip-name Walk --seconds 1 --samples 4
py -3 tools/blender/export_events.py --self-test
```

端到端：

```text
Blender -> character.glb + events.json
        -> anim_pack
        -> character.skeleton.json + Walk.ab + anim_manifest.json
        -> C++ runtime / anim_pack_demo / bgfx_demo
        -> 播放、截图、根运动、事件
```

真实 Blender 同骨架资产（仓库不提交）使用 §8.5 的模板；合成夹具已验证数据与运行时链路，
真实视觉截图需美术提供 `.blend`/`.glb` 后补齐。

### 10.3 视觉验收

- 同骨架角色播放 Walk/Run/Idle；
- 骨骼无爆炸/塌缩；
- 脚底接触与源 Blender 动画一致；
- 循环首尾无明显跳变；
- 根运动方向与引擎坐标一致；
- 事件触发点与 Blender 时间轴 Marker 对应。

## 11. 风险与开放问题

| 风险 | 影响 | 缓解 |
|------|------|------|
| glTF joint 顺序不是拓扑序 | 骨架构建失败/骨索引错 | 拓扑排序 + 明确映射；测试乱序 fixture |
| inverseBind 缺失或错误 | 调色板/蒙皮错 | 优先读 skin；缺失时计算并测试 bind pose |
| 非 joint 父链/Armature 变换 | 根骨姿态偏 | 明确合并规则或要求导出规范化；测试 |
| 坐标手性混用 | 左右/朝向错误 | 打包期一次性转换；双次镜像测试 |
| 插值/四元数符号 | 抖动/翻转 | 转换规则 + 连续性修正 + 采样测试 |
| 根运动重复应用 | 角色漂移 | 明确由谁消费；root bone 唯一 |
| 事件名哈希不可反查 | 无法绑定回调 | manifest 保存名字；运行时事件表 |
| Transition 生命周期 | 悬空事件/崩溃 | 每角色 Transition；文档约束；测试 |
| cgltf 依赖 | 构建复杂 | 只给离线工具；CMake option；保留替代解析器接口 |
| Blender 导出优化 | 关键帧丢失 | 文档固定设置；真实资产验收 |

开放问题（P8.2/P8.3 已按默认假设实现；真实 Blender 资产如有不同需在外部验收后调整）：

1. 目标引擎坐标系是 glTF 右手系，还是 Unity-like 左手系？
2. 项目是否已有 glTF/GLB 加载器可以复用离线导入？
3. 是否需要跨骨架 retarget 作为 P8 的硬需求？
4. Blender 根运动是在 root bone 还是 Armature Object？
5. 事件是否需要参数（伤害、音效 ID），还是只需要名字？

默认假设：glTF 坐标、同骨架、manifest 指定的 root bone、事件只需要名字。P8.2 的
`--coordinate gltf|mirrored` 已覆盖两种坐标；跨骨架运行时核心见 P9，真实不同拓扑
资产仍需外部视觉验收。

## 12. 预计文件变更

新增：

```text
docs/18-Blender资产管线与运行时补齐.md
include/anim/integration/AnimationAssets.h
include/anim/integration/AnimationManifest.h
include/anim/integration/AnimationClipLibrary.h
include/anim/integration/AnimationTransitions.h
include/anim/integration/Json.h
src/integration/AnimationManifest.cpp
src/integration/AnimationClipLibrary.cpp
src/integration/AnimationTransitions.cpp
src/integration/Json.cpp
tools/anim_pack/main.cpp
tools/anim_pack/AnimPack.h/.cpp
tools/anim_pack/EventSidecar.h/.cpp
tools/anim_pack/GltfImporter.h/.cpp
tools/anim_pack/ClipBuilder.h/.cpp
tools/anim_pack/SkeletonWriter.h/.cpp
tools/anim_pack/ManifestWriter.h/.cpp
tools/anim_pack/JsonWriter.h/.cpp
tools/anim_pack/third_party/cgltf.h
tools/anim_pack/third_party/cgltf_impl.cpp
tools/anim_pack/CMakeLists.txt
tools/blender/export_events.py
samples/anim_pack_demo/main.cpp
tests/test_animation_manifest.cpp
tests/test_animation_runtime.cpp
tests/test_anim_pack.cpp
tests/fixtures/anim_pack/simple_rig.gltf
tests/fixtures/anim_pack/simple_rig.glb
tests/fixtures/anim_pack/simple_rig_manifest.json
tests/fixtures/anim_pack/simple_rig_events.json
tests/fixtures/anim_pack/non_joint_parent.gltf
```

修改：

```text
CMakeLists.txt                 // ANIM_BUILD_ANIM_PACK / cgltf 接入
tests/CMakeLists.txt           // 新增 test_animation_manifest / test_anim_pack
README.md                      // 文档索引与构建命令
docs/11-资源转换器.md          // 增加 Blender/glTF 章节
docs/12-引擎集成-bgfx.md       // 增加 manifest/Blender 接线
docs/15-API参考.md             // AnimationAssets/Manifest 签名
include/anim/animancer/AnimancerAnimator.h
src/animancer/AnimancerAnimator.cpp
tools/anim_converter/SkeletonLoader.*  // inverseBind / 拓扑校验
tools/anim_converter/AvatarLoader.*    // JSON 头文件迁移
```

可选：

```text
tests/fixtures/anim_pack/blender_minimal.glb   // 外部 Blender 资产，当前未提交
samples/bgfx_demo/manifest_demo.*
```

## 13. 完成定义

P8 完成必须同时满足：

- [ ] Blender 导出的 glTF 能生成可播放的 `.ab`；
- [ ] 骨架顺序、rest pose、inverseBind 在运行时正确；
- [ ] 同骨架 Walk/Run/Idle 至少三个 Clip 播放正常；
- [ ] loop/fade/speed/start/rootBone/事件由 manifest 驱动；
- [ ] 事件在循环中正确触发且不串角色；
- [x] `AnimancerAnimator` 常用门面可用；
- [x] 原有 195/195 测试保持通过；P8.3 完成后主构建 202/202、pack 213/213、EnTT 203/203；P8.4 完成后主构建 209/209、pack 220/220、EnTT 210/210；P8.5 完成后主构建 213/213、pack 224/224、EnTT 214/214；P9 运行时核心完成后主构建 214/214、pack 225/225、EnTT 215/215；P8.5d 完成后主构建 235/235、pack 246/246、EnTT 236/236；
- [x] 合成夹具的文档命令可复现；真实 Blender 资产命令模板见 §8.5/§10.2；
- [ ] 没有把 cgltf/glTF 解析依赖引入运行时核心库；
- [x] 跨骨架 Humanoid 的 C++ 运行时核心已实现并标注为 P9 可选项；真实不同拓扑视觉验收仍待外部资产。

## 14. 实施记录

按阶段完成后在此追加：

| 阶段 | 状态 | 产物 | 验证 |
|------|------|------|------|
| P8.1 契约与夹具 | 完成 | `AnimationAssets`/`AnimationManifest`/共享 `Json`；`SkeletonLoader` 可选 `inverseBind`；`tests/fixtures/anim_pack/` | 主构建 202/202，EnTT 203/203 |
| P8.2 `anim_pack` | 完成 | cgltf 1.14、GLB/glTF skeleton + animation 导入、LINEAR/STEP/CUBICSPLINE、四元数连续性、`--coordinate gltf|mirrored`、`.ab`/skeleton/manifest 输出 | `ANIM_BUILD_ANIM_PACK=ON` 209/209 |
| P8.3 Blender 工作流 | 完成（真实资产待外部验收） | `tools/blender/export_events.py`、`--events` sidecar、完整 §8 文档、`anim_pack_demo`、运行时播放测试 | pack 测试 213/213；合成 GLB 播放通过；真实 Blender 资产未提交 |
| P8.4 manifest + 事件 | 完成 | `AnimationClipLibrary`、`AnimationTransitions`、每角色事件副本、`rootBone` 绑定、`anim_pack_demo --manifest`、运行时循环/跨角色事件测试 | 主构建 209/209；pack 测试 220/220；EnTT 210/210；合成 GLB manifest 播放与事件打印通过 |
| P8.5 门面 | 完成 | `AnimancerAnimator` fade/stop/isPlaying/layer 转发；4 个门面测试 | 主构建 213/213；pack 测试 224/224；EnTT 214/214 |
| P8.5b 门面补全 | 完成 | `play(clip/state)`、`tryPlay(key)`、`stop(key/clip)`、`isPlaying(key/clip)`；按注册状态原 Layer 播放，`isPlayingClip` 搜索所有状态 | 主构建 225/225；pack 236/236；EnTT 226/226 |
| P8.5c 组件等价补全 | 完成 | `AnimancerLayer` 跨层状态克隆；`NamedAnimancerAnimator`；`evaluate/speed/pauseGraph` | 主构建 232/232；pack 243/243；EnTT 233/233 |
| P8.5d Layer key-chain 补全 | 完成 | `getState(key)` 沿跨层 key 链查找；`getOrCreateState(clip/transition)` 在目标 Layer 创建独立状态；`allowSetClip` | 主构建 235/235；pack 246/246；EnTT 236/236 |
| P8.6 验收 | 文档/命令完成；真实视觉待外部 | README/docs 11/12/17 状态、合成夹具端到端命令、Blender 同骨架模板 | 主构建 213/213；pack 224/224；EnTT 214/214；真实 Blender `.glb` 截图未提交 |
| P9 跨骨架运行时 | C++ 核心完成（可选） | `RetargetClipNode`/`RetargetClipState`/`RetargetClipTransition` | 主构建 214/214；pack 225/225；EnTT 215/215；真实不同拓扑视觉待外部 |
