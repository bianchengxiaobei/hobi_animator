# hobi_animator

> Unity **Animancer Pro v7.4.2** 的 C++ 重实现（项目内部代号 `AnimancerCpp`）。

一个引擎无关的 C++20 动画运行时：实现 Animancer 的**播放、过渡、混合、状态机、根运动、人形重定向、Animation Jobs/IK** 等运行时能力，只产出 **Pose / 骨骼矩阵调色板**，不绑定渲染、物理或 ECS。

- 核心库 `anim::anim` 唯一必需依赖是 [glm](https://github.com/g-truc/glm)
- 核心代码不包含任何 bgfx / Unity 头文件，可独立编译与测试
- 动画资产离线烘焙：Unity `.anim` 或 Blender glTF/GLB → 自有 `.ab` 二进制
- 可选组件：bgfx 演示、EnTT 适配层、并行 Job 调度器

范围与验收以 [17-功能范围与验收](docs/17-功能范围与验收.md) 为准。

## 特性

| 模块 | 能力 |
|------|------|
| 播放 | 单 Clip 播放、时间/归一化时间/速度/暂停、循环 |
| 图与层 | 自实现 PlayableGraph、Layer 混合（覆盖 / 加法 / AvatarMask） |
| 过渡 | Transition、多种 FadeMode、淡入淡出曲线、CustomFade |
| 状态 | State 层级、StateDictionary（按 key 复用）、weightless 克隆 |
| 混合器 | Manual（手动权重）、Linear（1D）、Directional（2D 极坐标）、Cartesian（2D 笛卡尔） |
| 事件 | 时间/归一化时间回调、OnEnd、名字索引、正反向触发 |
| 状态机 | FSM、WithDefault、InputBuffer、StateChange、StateBehaviour |
| 根运动 | 从 Hips 提取，`consumeRootMotion()` 交给引擎决定是否应用 |
| 人形重定向 | 完整肌肉空间（Route A），支持跨比例骨架 |
| Jobs / IK | `IAnimationJob` + `IJobScheduler`、TwoBoneIK、Damping、Lean |
| 资产 | `.ab` 编解码、`skeleton.json`、`anim_manifest.json`、Blender 事件 sidecar |

## 目录结构

```
.
├─ docs/                 # 设计文档（先写文档，再按文档实现）
├─ include/anim/         # 公共头文件
│  ├─ core/              # 基础类型、序列化、对象池
│  ├─ clip/              # 曲线、Clip、事件标记、.ab 格式
│  ├─ skeleton/          # Skeleton / Bone / Pose / 混合 / 调色板
│  ├─ avatar/            # 人形 Avatar、肌肉空间、重定向
│  ├─ graph/             # Playable 图（Node / Handle / Output）
│  ├─ animancer/         # 门面层（Playable / Layer / State / Transition / Fade / Events / FSM）
│  ├─ jobs/              # IAnimationJob + IJobScheduler + IK / Damping / Lean
│  └─ integration/       # 引擎/主循环集成、调色板上传、坐标转换、可选 ECS 适配
├─ src/                  # 与 include 镜像的实现
├─ tools/anim_converter/ # .anim / .fbx.meta -> .ab 离线转换器
├─ tools/anim_pack/      # glTF/GLB -> .ab + skeleton + manifest（可选）
├─ tools/blender/        # 可选 Blender Marker/事件 sidecar 导出脚本
├─ tests/golden/         # 离线回归 fixtures
└─ samples/              # bgfx_demo、humanoid_retarget_demo、anim_pack_demo
```

## 构建与测试

依赖：CMake ≥ 3.20、C++20 编译器（MSVC 需 `/utf-8`，已内置）。

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- glm：优先 `ANIM_GLM_DIR`，其次 `find_package(glm)`，最后 `FetchContent` 自动拉取。
- googletest：仅测试构建时由 FetchContent 自动拉取。

| CMake 选项 | 默认 | 说明 |
|------------|------|------|
| `ANIM_BUILD_TESTS` | `ON` | 单元测试 |
| `ANIM_BUILD_TOOLS` | `ON` | `anim_converter` / `anim_calibrate` / `anim_bench` |
| `ANIM_BUILD_ANIM_PACK` | `OFF` | glTF/GLB 打包器（默认不编译 cgltf） |
| `ANIM_BUILD_SAMPLES` | `OFF` | bgfx 演示（需 bgfx + assimp） |
| `ANIM_ENABLE_ENTT` | `OFF` | 可选 EnTT ECS 适配层 |

当前测试基线：主构建 240/240，`ANIM_BUILD_ANIM_PACK=ON` 时 251/251，EnTT 241/241。

## 最小使用示例

```cpp
#include "anim/animancer/AnimancerAnimator.h"
#include "anim/clip/ClipIO.h"

// 1) 读取离线烘焙的 Clip
std::vector<uint8_t> bytes = ReadFileBytes("Walk.ab");
auto clip = anim::decodeClip(bytes);

// 2) 准备骨架：从 skeleton.json 加载，或由引擎导入器构建
//    约束：bones 按拓扑序排列（父索引严格小于子索引），名字唯一
anim::Skeleton skeleton;
int hips = skeleton.addBone(anim::hashString("Hips"), -1, hipsRest);
int spine = skeleton.addBone(anim::hashString("Spine"), hips, spineRest);

// 3) 每个角色一个 AnimancerAnimator
anim::AnimancerAnimator animator;
animator.initialize(skeleton);
animator.play(*clip, 0.25f, anim::FadeMode::FixedSpeed);

// 4) 主循环
animator.update(dt);                       // 求值 + 更新调色板
uploadBoneMatrices(animator.palette());    // std::vector<glm::mat4>，glm 列主序
auto delta = animator.consumeRootMotion(); // 是否应用到角色由引擎决定
```

需要按 manifest 批量加载时，可使用 `AnimationClipLibrary` + `AnimationTransitions`（见 `include/anim/integration/`）。

## 资产管线

```text
Unity .anim + .fbx.meta ── anim_converter ──> *.ab

Blender / 其他 DCC ── glTF/GLB ── anim_pack ──> *.ab + *.skeleton.json + anim_manifest.json
```

- `.ab` 中的 boneIndex 必须与运行时骨架顺序一致；换模型时先用工具导出骨架，再转换动画。
- 同骨架动画使用 `BoneLocalClip` 即可，不需要 Humanoid 管线；只有跨骨架重定向才需要 `HumanoidAvatar`。
- manifest 提供 loop / fade / speed / start / rootBone / 事件名等运行时配置。

命令示例：

```powershell
# Unity .anim -> .ab
build/Release/anim_converter.exe --batch <anim目录> --out-dir build/ab

# glTF/GLB -> .ab + skeleton + manifest
cmake -S . -B build_pack -DANIM_BUILD_ANIM_PACK=ON
cmake --build build_pack --config Release --target anim_pack anim_pack_demo
build_pack/tools/anim_pack/Release/anim_pack.exe `
  --input tests/fixtures/anim_pack/simple_rig.glb --out-dir build_pack/anim_out
```

真实 Blender 资产流程见 [18-Blender资产管线与运行时补齐](docs/18-Blender资产管线与运行时补齐.md)。

## 接入自己的引擎

1. **链接**：`add_subdirectory(hobi_animator)` 后 `target_link_libraries(YourEngine PRIVATE anim::anim)`。
2. **每帧**：`animator.update(dt)`，然后上传 `palette()`；结束前（并行时）等待 Job 完成。
3. **坐标系**：Skeleton/Pose/Palette 统一使用引擎坐标系，在导入期一次性转换；不要每帧转换（`Coordinate.h` 提供 Z 镜像原语）。
4. **调色板**：`palette()` 是 glm 列主序矩阵。`transposePalette` 仅用于 bgfx/HLSL 的行主序约定，OpenGL/Vulkan 直接上传即可。
5. **线程**：核心只依赖 `IJobScheduler`（默认 Inline/线程池），可注入引擎调度器；EnTT 适配层可选，核心不绑定 ECS。
6. **运行时库**：根 CMake 为 bgfx 演示把 MSVC 运行时固定为 `/MT`，嵌入其他工程时请改成与引擎一致（如 `/MD`）。
7. **语言边界**：核心没有 `extern "C"` 导出面；C#/Lua 等宿主需自行封装 shim。

更多细节见 [12-引擎集成-bgfx](docs/12-引擎集成-bgfx.md) 与 [01-总体架构](docs/01-总体架构.md)。

### 可选：bgfx 演示

```powershell
cmake -S . -B build -DANIM_BUILD_SAMPLES=ON `
      -DANIM_BGFX_ROOT=<bgfx> -DANIM_ASSIMP_ROOT=<assimp>
cmake --build build --config Release --target bgfx_demo humanoid_retarget_demo anim_bench
build/samples/bgfx_demo/Release/bgfx_demo.exe --clip build/ab/Walk.ab `
  --frames 65 --screenshot shot --screenshot-frame 50 --camera-yaw 25
```

> bgfx 预编译 Debug 库在部分机器上 `bgfx::init` 会挂起，演示请用 Release。

## 文档索引

| 文档 | 内容 |
|------|------|
| [00-愿景与范围](docs/00-愿景与范围.md) | 目标、非目标、范围边界、术语 |
| [01-总体架构](docs/01-总体架构.md) | 分层、模块依赖、数据流、更新循环、与 Animancer 对应 |
| [02-核心数据类型](docs/02-核心数据类型.md) | glm 类型封装、Transform、Pool、序列化 |
| [03-动画数据与曲线](docs/03-动画数据与曲线.md) | Keyframe/Curve(Unity Hermite)、Clip、事件、`.ab` 格式 |
| [04-骨架与位姿](docs/04-骨架与位姿.md) | Skeleton/Bone/Pose、混合、调色板、根运动提取 |
| [05-人形肌肉空间与重定向](docs/05-人形肌肉空间与重定向.md) | HumanBone、肌肉表、muscle↔local、T-Pose、IK、对拍 |
| [06-Playable图](docs/06-Playable图.md) | Graph/Node/Handle/Output、加权、双阶段更新、命令 |
| [07-Animancer门面](docs/07-Animancer门面.md) | Playable/Layer/State/StateDictionary/Transition/Fade/Events |
| [08-混合器](docs/08-混合器.md) | Manual/Linear/Directional/Cartesian 权重算法 |
| [09-状态机FSM](docs/09-状态机FSM.md) | StateMachine、WithDefault、InputBuffer、StateChange |
| [10-根运动与Animation-Jobs](docs/10-根运动与Animation-Jobs.md) | 根运动重定向、TwoBoneIK、Damping、Lean、Animated 属性 |
| [11-资源转换器](docs/11-资源转换器.md) | `.anim`/`.fbx.meta` 解析、切线语义、Avatar 配置、`.ab` 写出 |
| [12-引擎集成-bgfx](docs/12-引擎集成-bgfx.md) | 主循环钩子、调色板输出、蒙皮、线程 |
| [13-测试与黄金数据](docs/13-测试与黄金数据.md) | 导出脚本、逐帧对拍、容差 |
| [14-实施路线图](docs/14-实施路线图.md) | 里程碑、任务清单、DoD、风险 |
| [15-API参考](docs/15-API参考.md) | 头文件级公共 API 签名 |
| [16-任务调度与ECS集成](docs/16-任务调度与ECS集成.md) | Animation Jobs ↔ Job System、ECS 组件/系统/并行策略 |
| [17-功能范围与验收](docs/17-功能范围与验收.md) | 范围基准、已接受简化、三层验收、支持矩阵、发布门槛 |
| [18-Blender资产管线与运行时补齐](docs/18-Blender资产管线与运行时补齐.md) | manifest、skeleton.json、anim_pack、Blender sidecar、跨骨架运行时 |

## 实现状态

- [x] P0 基础（core / clip / curve / 序列化）
- [x] P1 骨架与位姿；便利功能（TimeSynchronizer、DirectionalAnimationSet、ExitEvent 等）
- [x] P2 Playable 图
- [x] P3 Animancer 门面 + 混合器 + CustomFade + ClipTransitionSequence
- [x] P4 人形肌肉空间与 Unity 对拍（无 Editor 的 `.fbx.meta` 路径、RetargetFrom、IK goals）
- [x] P5 FSM / 根运动 / Animation Jobs
- [x] P6 `.anim` 转换器（`.ab` v3、批量、实测资源）
- [x] P7 bgfx 集成、GPU 蒙皮、手性转换、ThreadPool 并行、SSE2 混合、基准
- [x] P8 Blender/glTF 资产管线（manifest、`anim_pack`、Blender sidecar、运行时事件/根骨绑定）
- [x] P9 可选跨骨架运行时重定向（`RetargetClipNode` / `RetargetClipState` / `RetargetClipTransition`）

## 范围与非目标

核心提供运行时能力，**不**包含：

- Unity Editor、Inspector、MonoBehaviour 组件体系
- `RuntimeAnimatorController` / Animator 状态机 / Timeline / `PlayableAsset`
- Unity 原生 `PlayableGraph` 句柄
- 渲染、蒙皮着色器、物理模拟（只输出 Pose / 骨骼矩阵调色板）

## 声明

本项目是 Animancer 的独立 C++ 重实现，与原作者 Kybernetik 无隶属关系。仓库不包含 Unity Animancer Pro 资产；文档中的原项目源码/行号引用针对本地参考副本，需要自行持有相应授权。
