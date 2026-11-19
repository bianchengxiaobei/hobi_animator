# AnimancerCpp

Unity **Animancer Pro v7.4.2** 的 C++ 重实现，用于自研 C++ 引擎（glm + bgfx）。

本仓库的目标：用 C++ 实现 Animancer 的**动画播放、过渡、混合、状态机、根运动、人形重定向、Animation Jobs/IK** 等运行时能力，并提供把 Unity `.anim` 资源离线转换为自有二进制的工具链。范围与验收以 [17-功能范围与验收](docs/17-功能范围与验收.md) 为准。

> 原始参考实现位于同仓的 Unity 工程：
> `Assets/Plugins/Animancer`（下称“原项目”）。
> 所有设计文档都会标注对应的原项目源文件与行号；当前实现以**功能等价**为目标，Unity 2019 数值逐帧对拍为可选精确模式。

## 关键决策（已确认）

| 项 | 决策 |
|----|------|
| 目标 | 自研 C++ 引擎 |
| 数学库 | 直接使用 **glm**（不重造向量/四元数） |
| 渲染 | **bgfx**（本库只输出骨骼矩阵调色板，不依赖渲染） |
| 骨架 | 本库自建 Skeleton/Pose 数据结构 |
| 动画资源 | 不要求兼容 Unity 运行时；用**离线转换器**把 `.anim` 转成自有 `.ab` 二进制 |
| 人形重定向 | **Route A：完整肌肉空间（muscle space）管线**，与 Unity 一致 |
| 功能范围 | 运行时全量（过渡/淡入淡出/事件/FSM/根运动/Animation Jobs）；不含 Unity Editor/Controller/PlayableAsset |
| Animation Jobs 执行 | 模型为 `IAnimationJob` + `IJobScheduler`（等价 Unity C# Job System）；核心**不绑定 ECS**。默认单线程后端，可换 enkiTS/Taskflow/引擎调度器（见 16） |
| ECS | **EnTT 可选**：仅作 ECS 适配层（组件/系统），需 `ANIM_ENABLE_ENTT`；**EnTT 不是并行 Job System**，并行仍由 `IJobScheduler` 负责（见 16） |

## 目录结构

```
AnimancerCpp/
├─ docs/                 # 设计文档（先写文档，再按文档实现）
├─ include/anim/         # 公共头文件
│  ├─ core/              # 基础类型、序列化、对象池
│  ├─ clip/              # 曲线、Clip、事件标记、.ab 格式
│  ├─ skeleton/          # Skeleton / Bone / Pose / 混合 / 调色板
│  ├─ avatar/            # 人形 Avatar、肌肉空间、重定向
│  ├─ graph/             # Playable 图（Node / Handle / Output）
│  ├─ animancer/         # 门面层（Playable / Layer / State / Transition / Fade / Events / FSM）
│  ├─ jobs/              # IAnimationJob + IJobScheduler + IK/Damping/Lean
│  └─ integration/       # 与引擎/主循环的集成（含可选 ECS 适配）
├─ src/                  # 与 include 镜像的实现
├─ tools/anim_converter/ # .anim / .fbx.meta -> .ab 离线转换器
├─ tools/anim_pack/      # glTF/GLB -> .ab + skeleton + manifest（可选）
├─ tools/blender/        # 可选 Blender Marker/事件 sidecar 导出脚本
├─ tests/golden/         # Unity 4.7 离线回归 fixtures（Unity 2019 为可选精确模式）
└─ samples/              # bgfx_demo 与 headless anim_pack_demo
```

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
| [09-状态机FSM](docs/09-状态机FSM.md) | StateMachine1/2、WithDefault、InputBuffer、StateChange |
| [10-根运动与Animation-Jobs](docs/10-根运动与Animation-Jobs.md) | 根运动重定向、TwoBoneIK、Damping、Lean、Animated 属性 |
| [11-资源转换器](docs/11-资源转换器.md) | `.anim`/`.fbx.meta` 解析、切线语义、Avatar 配置、`.ab` 写出、glTF/Blender sidecar |
| [12-引擎集成-bgfx](docs/12-引擎集成-bgfx.md) | 主循环钩子、调色板输出、bgfx 蒙皮、线程 |
| [13-测试与黄金数据](docs/13-测试与黄金数据.md) | Unity 导出脚本、逐帧对拍、容差 |
| [14-实施路线图](docs/14-实施路线图.md) | P0-P7 里程碑、任务清单、DoD、风险 |
| [15-API参考](docs/15-API参考.md) | 头文件级公共 API 签名 |
| [16-任务调度与ECS集成](docs/16-任务调度与ECS集成.md) | Animation Jobs ↔ Job System、是否用 EnTT、ECS 组件/系统/并行策略 |
| [17-功能范围与验收](docs/17-功能范围与验收.md) | 当前唯一范围基准：目标/非目标、已接受简化、三层验收、支持矩阵与发布门槛 |
| [18-Blender资产管线与运行时补齐](docs/18-Blender资产管线与运行时补齐.md) | P8.1-P8.6：manifest、`skeleton.json inverseBind`、cgltf `anim_pack`、Blender Marker sidecar、运行时事件/根骨绑定、`anim_pack_demo`、`AnimancerAnimator` 门面；P9 可选跨骨架运行时核心；真实 Blender 视觉验收待外部资产 |

## 实现状态

- [x] 源码研读与架构设计
- [x] 文档体系编写（本目录）
- [x] P0 基础（core / clip / curve / 序列化）
- [x] P1 骨架与位姿
- [x] P2 Playable 图
- [x] P3 Animancer 门面（Node/State/Layer/Transition/Fade/Events）
- [x] P3-b 混合器（Manual/Linear/Directional/Cartesian）+ CustomFade + ClipTransitionSequence
- [x] P4 人形肌肉空间（表/Avatar/MuscleSpace/采样；P4.1 Unity dump → `anim_calibrate`、P4.2 全身 dump、无 Editor 的 `.fbx.meta` + Unity 4.7 源码离线轴表/`axes`、`AvatarBuilder` 与 Walk/Run/Idle 各 5 帧 × 54 骨 source 回归（含 Hips RootT/RootQ、四个 goal T/Q）、可选 `applyHumanoidGoals` 两骨 IK、`applyHumanoidTwist` twist 分配、`applyHumanoidLookAt` 视线转向、完整 `retargetHumanoidFromPose` RetargetFrom 已就绪；Unity 2019 数据按 17 作为可选精确模式）
- [x] P5 FSM / 根运动 / Animation Jobs
- [x] P1 便利功能：TimeSynchronizer/Group、DirectionalAnimationSet(8)/DirectionalClipTransition、
      MixerParameterTween、ExitEvent、DontAllowFade、StateSelector/IPrioritizable
- [x] P6 `.anim` 转换器（含 `.ab` v3 读写、批量、17 个资源实测）
- [x] P7 bgfx 集成（GPU 蒙皮 demo）、手性转换、ThreadPool 并行、xform float4/SSE2
      混合、单输入 passthrough、TRS palette、性能基准、AnimatedValue 属性通道、可选 EnTT 适配
- [x] P8.1 Blender/glTF 资产契约：`AnimationAssets`/manifest 读取、`skeleton.json`
      可选 `inverseBind`（缺失时按 rest pose 计算）、合成 glTF 夹具
- [x] P8.2 `anim_pack`：cgltf 解析 GLB/glTF，提取 skin/动画，支持
      LINEAR/STEP/CUBICSPLINE 与 `--coordinate gltf|mirrored`，输出 `.ab`、skeleton、manifest
- [x] P8.3 Blender 工作流：完整导出文档、`tools/blender/export_events.py` Marker sidecar、
      `anim_pack --events`、`anim_pack_demo` 与打包结果运行时播放测试（真实 Blender 资产待外部验收）
- [x] P8.4 manifest 运行时：`AnimationClipLibrary`/`AnimationTransitions`、每角色事件上下文、
      `rootBone` 根运动绑定、`anim_pack_demo --manifest` 示例
- [x] P8.5 `AnimancerAnimator` 门面：clip/transition fade、stop、isPlaying、layer 转发，
      未初始化安全，新增 4 个测试
- [x] P8.6 验收文档：合成夹具端到端命令、Blender 同骨架验收模板、测试基线；真实
      Blender 资产视觉截图待外部文件
- [x] P9 可选跨骨架运行时：`RetargetClipNode`/`RetargetClipState`/`RetargetClipTransition`，
      源 BoneLocal Clip 经 HumanoidAvatar 重定向到目标骨架
- [x] `SoloAnimation` 单 Clip 门面：直接采样、播放/暂停、速度/时间、Pose/Palette
- [x] `AnimancerAnimator` 门面补全：`play(state)`、`tryPlay(key)`、`stop(key/clip)`、`isPlaying(key/clip)`；状态按原 Layer 播放，图暂停时 `isPlaying` 返回 false
- [x] `AnimancerLayer` 跨 Layer 创建/播放：`getState(key)` 沿 key 链查找；目标 Layer 按原 `GetOrCreateState(AnimancerState)` 克隆状态
- [x] `kEulerXYZ`：按 Unity `axes.h`/`quaternion.h` 实现精确 Euler 转换，type 3 不再回退 qtan
- [x] 可选 mass-center root：`HumanoidRootMode::MassCenter`，按默认骨质量/肢体朝向计算根，朝向相对 setup/rest，独立 root 骨写 mass-root，Hips 世界空间调整支持 Armature 父级；goal 同步使用 mass-center root；默认仍为 Hips-root
- [x] `NamedAnimancerAnimator`：名字→Clip 注册、默认动画、按名字播放/停止/淡入
- [x] `AnimancerAnimator` 便利 API：`evaluate`、`speed`、`pauseGraph/unpauseGraph`
- [x] 当前基线：主构建 240/240，`ANIM_BUILD_ANIM_PACK=ON` 时 251/251，EnTT 241/241；
      Release 200×64 串行约 1.9–2.0 ms/frame，并行约 0.35–0.47 ms/frame；
      Unity 2019 精确对拍为可选模式

> 约定：模块实现与本文档对应章节、原项目源码保持可追溯；关键算法以原项目源码为参考，范围与验收以 17 为准。

## 构建与测试

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- 依赖：CMake ≥ 3.20、C++20 编译器（MSVC 需 `/utf-8`，已内置）。
- glm：优先 `ANIM_GLM_DIR`（默认探测 `E:/Learn/glm`）→ `find_package(glm)` → `FetchContent`。
- googletest：`FETCHCONTENT` 自动拉取（仅测试构建）。
- 不使用 EnTT 时无需任何额外配置；需要 ECS 适配时加
  `-DANIM_ENABLE_ENTT=ON -DANIM_ENTT_DIR=E:/Learn/entt`。

### P7 bgfx demo / 基准（可选）

```powershell
cmake -S . -B build -DANIM_BUILD_SAMPLES=ON `
      -DANIM_BGFX_ROOT=E:/Learn/bgfx -DANIM_ASSIMP_ROOT=E:/Learn/assimp
cmake --build build --config Release --target bgfx_demo humanoid_retarget_demo anim_bench
build/samples/bgfx_demo/Release/bgfx_demo.exe `
  --clip build/ab_test/Humanoid-Walk.ab --frames 65 --screenshot shot --screenshot-frame 50 `
  --camera-yaw 25 --shaders build/samples/bgfx_demo/Release/shaders
build/samples/bgfx_demo/Release/humanoid_retarget_demo.exe `
  --clips build/ab_test/Humanoid-Walk.ab,build/ab_test/Humanoid-Run.ab,build/ab_test/Humanoid-Idle.ab `
  --clip-frames 120 --screenshot-frame 30 --screenshot build/retarget_acceptance `
  --shaders build/samples/bgfx_demo/Release/shaders --strict
build/Release/anim_bench.exe --characters 200 --bones 64 --parallel
```

> bgfx demo 请用 **Release**：本机预编译的 bgfx Debug 库在 `bgfx::init` 会挂起
> （Release 正常）。核心库与测试仍默认构建 Debug。
>
> `humanoid_retarget_demo` 的验收命令、截图和端点误差见 `docs/17` §6.3；
> bgfx mesh/palette 的 Walk/Run/Idle 与 SpiderBot 截图见 §6.4。
> 手臂/身体间距请同时看 `--camera-yaw 90` 侧视图；前 3/4 视角的前后摆臂会投影重叠。
> 非交互截图建议加 `--fixed-dt`（30 fps 用 `0.0333333`），否则高帧率下可能只采样到 Clip 开头。

### P8 anim_pack（可选）

```powershell
cmake -S . -B build_pack -DANIM_BUILD_ANIM_PACK=ON
cmake --build build_pack --config Release --target anim_pack anim_pack_demo
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

`anim_pack` 只链接离线工具；默认构建不编译 cgltf。`--coordinate mirrored`
会同时镜像骨架 rest/inverseBind 和动画 translation/rotation。事件 sidecar 的
Blender 导出流程见 `docs/18` §8.3；真实 Blender 同骨架资产与 `bgfx_demo` 截图模板
见 `docs/18` §8.5。仓库内的 `simple_rig.glb` 是合成夹具，视觉验收仍需真实资产。

