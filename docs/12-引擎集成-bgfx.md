# 12 · 引擎集成（bgfx）

L7 层：把动画系统接入你的 C++ 引擎。本库本身**不含任何 bgfx 头文件**；本章描述接线方式，代码只存在于 `integration/` 与 `samples/bgfx_demo/`。

## 1. 坐标与手性

- Unity 世界为**左手系、Y 上、+Z 前**；bgfx 本身不强制手性（取决于你的投影与视图矩阵）。
- 本库数学层（glm）不做手性假设。**手性转换放在集成层**：
  - 若引擎采用右手系，进入动画系统的模型数据与输出的调色板需统一转换（例如对 Z 取反、四元数相应变换）。
  - 建议在导入模型时一次性把骨架/rest pose 转到引擎坐标系，动画数据保持“跟随骨架”，避免每帧转换。
- 文档约定：**Skeleton/Pose/Palette 全部处于引擎坐标系**；转换器把 Unity `.anim` 的数值按“同一手性约定”搬运（纯标量/局部 TRS 通常无需改；仅当根空间/世界方向相关时需处理，见 05 §4）。

## 2. 集成对象

```cpp
// include/anim/hobi/HobiAnimator.h
class HobiAnimator {
public:
    void initialize(Skeleton& skeleton);   // 创建 PlayableGraph + Output
    void destroy();

    HobiPlayable& playable();
    HobiLayer* layer(int index);

    HobiState* play(const Clip&);
    HobiState* play(const Clip&, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    HobiState* play(ITransition&);
    HobiState* play(ITransition&, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    void stop();
    bool isPlaying() const;
    bool isPlaying(const Clip&) const;

    // 每帧调用：驱动 pre-update / 图求值 / post-update（事件）
    void update(float deltaTime);

    // 输出：蒙皮矩阵调色板（boneCount 个 Mat4）
    const std::vector<Mat4>& palette();

    // 根运动（由外部决定是否消费）
    RootMotionDelta consumeRootMotion();
};
```

数据流：
```
game loop(dt)
  └─ animator.update(dt)
       ├─ PlayableGraph.evaluate(dt)   // 双阶段 + 图求值（见 01/06）
       └─ computePalette(skeleton, pose, _palette)
  └─ renderer.updateBoneMatrices(_palette)  // bgfx 上传
```

## 3. 骨骼调色板输出

- 每帧计算 `Mat4 world_i * inverseBind_i`（见 04 §4），得 `palette[boneCount]`。
- 上传方式（任选，取决于你的蒙皮实现）：
  - **Uniform 数组**：`bgfx::setUniform(u_bones, palette.data(), boneCount)`（骨骼少时）。
  - **纹理**：把矩阵写入浮点纹理，着色器按骨索引采样（骨骼多时，如 4xN RGBA32F）。
  - **顶点属性索引 + SSBO**（若后端支持）。
- 动画系统不关心上传细节，只提供 `std::vector<Mat4>`。

## 4. 主循环对接

```cpp
void Game::update(float dt) {
    for (auto* a : _animators) a->update(dt);   // 或按需/按可见性
    // ... 渲染
}
```
- 固定步长/可变步长：`update(dt)` 支持任意 dt；事件与根运动依赖 dt。
- 暂停：`playable().pauseGraph()` / `unpauseGraph()`（对齐原项目）。
- 多实例：**每个角色一个 HobiPlayable**（原项目也是每 Animator 一个图）。

## 5. 线程与 Job（ECS 可选）

- 初版：单线程（主线程更新）。简单、与原始行为一致。
- 任务调度：核心只依赖 `IJobScheduler`（默认 `InlineJobScheduler` = 同步执行）；可注入并行后端（enkiTS/Taskflow/引擎自带）。
- ECS（可选）：若引擎用 **EnTT**，用组件存 `HobiAnimator*/Pose/JobList/Palette`，用系统遍历；但**并行仍由 `IJobScheduler` 执行**（EnTT 无线程池）。详见 [16](16-任务调度与ECS集成.md)。
- 优化现状：
  1. 跨角色并行图求值（角色彼此独立）。
  2. `Transform` 按 Unity `math::xform` 的 3×float4（t/q/s）对齐，Blend 的累加/归一化
     使用 SSE2；`MixerNode`/`LayerMixerNode` 单输入走 pose passthrough，零拷贝。
  3. `computePalette` 使用世界 TRS 组合（对齐 `SkeletonPoseComputeGlobal`），
     `Transform::toMatrix` 直接列缩放并写平移列，减少逐骨 4x4 乘法。
  4. 过程式节点（`ScriptPoseNode` + `IAnimationJob`）的单图内按角色/链并行列为**可选精确模式（性能优化）**（见 10/16）。

> 性能策略已定：**xform float4 + SSE2 已完成，不再以跨骨 SoA 为必做项**；
> 后续只评估单图内按链并行、曲线采样 profile 等可选精确模式（性能优化）。

- 同步点：调色板上传前必须 `IJobScheduler::waitAll()`。
- 线程安全注意：
  - L1 对象池非线程安全 → 每线程一个池或加锁。
  - 全局 `DeltaTime/FrameID`（原项目为静态）在多图并行时应改为“每图上下文”，见 06 §6。
  - `Pose` 读写用双缓冲，避免并行读写冲突。

## 6. 内存

- `Pose` 按骨架骨数预分配；每角色复用，不每帧分配。
- 混合使用临时缓冲：可 `thread_local` 复用或从池取。
- `palette` 预分配为 `boneCount`。

## 7. 与 bgfx 的边界（必须遵守）

- `include/anim/**` 与 `src/**` **禁止** `#include <bgfx/...>`。
- 仅 `samples/bgfx_demo/**` 与引擎侧代码可引用 bgfx。
- 这样动画库可独立编译、单元测试、用于无渲染工具（如转换器校验）。

## 8. 示例（伪代码，见 `samples/bgfx_demo`）

```cpp
// 引擎侧
Skeleton skel = LoadSkeleton("character.glb");
HobiAnimator anim;
anim.initialize(skel);

auto* clip = LoadClip("Walk.ab");
anim.play(*clip, 0.25f, FadeMode::FixedSpeed);

// 每帧
anim.update(dt);
if (skinningEnabled)
    uploadBones(anim.palette());
```

## 9. 文件清单

```
include/anim/hobi/HobiAnimator.h
include/anim/integration/Skinning.h          // 调色板上传（引擎无关接口）
samples/bgfx_demo/main.cpp                   // bgfx 接线示例
```

## 10. 验收

1. 一个模型能循环播放通用 Clip，位置正确。
2. 人形 Clip 在目标骨架上动作正确（对拍见 13）。
3. 交叉淡入、层混合、事件回调在引擎主循环中生效。
4. 调色板驱动 bgfx 蒙皮渲染正确（含手性转换）。

## 11. P7 实现现状

### 11.1 代码

| 文件 | 内容 |
|---|---|
| `include/anim/hobi/HobiAnimator.h` / `src/...cpp` | `initialize` 创建 PlayableGraph，`update(dt)` 先求值再 `computePalette`，`consumeRootMotion()` |
| `include/anim/integration/Skinning.h` / `src/integration/Skinning.cpp` | `SkinningPalette`、`ISkinningUploader`、`transposePalette`（glm 列主序 → bgfx 行主序） |
| `include/anim/integration/Coordinate.h` | `flipZ` 原语（Transform/Quat/Mat4/Pose/Palette），双次镜像=原值 |
| `include/anim/jobs/ThreadPoolJobScheduler.h` / `src/jobs/ThreadPoolJobScheduler.cpp` | 依赖计数任务图、`waitAll`、`parallelFor` |
| `include/anim/jobs/AnimatedValue.h` + `Clip::PropertyChannel` | 非骨骼属性通道（`.ab` v3），`bindProperty` 读取 |
| `src/skeleton/Blend.cpp` | xform float4 + SSE2 的 pose 累加/归一化、单输入快路径 |
| `src/skeleton/Palette.cpp` | 世界 TRS 组合后再转矩阵，减少逐骨 4x4 乘法 |
| `include/anim/integration/ecs/entt/*` | 可选 EnTT 组件/系统（`ANIM_ENABLE_ENTT`） |
| `samples/bgfx_demo/` | Win32 + bgfx + assimp，加载 FBX、GPU 蒙皮、播放 `.ab` |
| `tools/anim_bench/` | N 角色 × B 骨基准（串行/并行） |

### 11.2 调色板上传（纹理）

`u_bones` uniform 数组受 bgfx 常量缓冲限制（D3D11 默认 1024 个 float，即 64 个 mat4），
DefaultHumanoid 合并 helper 后仍有 88 根骨骼（>64），必须用纹理：

- 纹理 `4 × boneCount`、`RGBA32F`，每行一个 mat4 的 4 个行向量。
- 顶点着色器按骨索引采样 4 个 texel 重建 `mat4`（`mat4(row0,row1,row2,row3)`，对应 HLSL
  `mul(M, v)` 的行主序）。
- 每帧 `transposePalette` 后 `updateTexture2D`，再 `setTexture`。
- 顶点属性：`BLENDINDICES`（`uvec4`，4×u8）+ `BLENDWEIGHT`（`vec4`，4×u8 normalized）。
  **权重语义必须写 `BLENDWEIGHT`**；写成 `TEXCOORD0` 会读到 0 并导致顶点塌缩。

### 11.3 Demo

```bash
# 1) 转换 .ab（见 11）
build/Debug/anim_converter.exe --batch ../Assets/Plugins/Animancer/Examples --out-dir build/ab_test

# 2) 构建 demo（bgfx 预编译 Debug 库在本机有兼容问题，请用 Release）
cmake -S . -B build -DANIM_BUILD_SAMPLES=ON \
      -DANIM_BGFX_ROOT=E:/Learn/bgfx -DANIM_ASSIMP_ROOT=E:/Learn/assimp
cmake --build build --config Release --target bgfx_demo

# 3) 运行（工作目录随意，shaders 由 CMake 拷到 exe 旁）
build/samples/bgfx_demo/Release/bgfx_demo.exe \
  --clip build/ab_test/Humanoid-Walk.ab \
  --shaders build/samples/bgfx_demo/Release/shaders \
  --camera-yaw 25 --frames 65 --screenshot shot --screenshot-frame 50
```

- 默认模型：`Assets/.../Default Humanoid/DefaultHumanoid.fbx`。默认会自动加载
  `tests/golden/DefaultHumanoid.source.avatar.json`（没有该文件时回退 `.fbx.meta`）；
  自定义模型可显式传 `--avatar avatar.json`。
- FBX 导入归一化：Assimp `GlobalScale=0.01`（cm→m）、X 轴镜像到运行时手性、合并
  `$AssimpFbx$` helper 节点；蒙皮 `inverseBind` 和顶点同步变换，未蒙皮的眼部 mesh 绑定到自身节点骨。
- 通用模型需要先用 demo 导出骨架，再用该骨架转换 `.ab`（通道里的 boneIndex 必须与运行时
  骨架顺序一致）：

```bash
bgfx_demo.exe --model "../Assets/Plugins/Animancer/Examples/Art/Spider Bot/SpiderBot.FBX" \
              --export-skeleton build/spider_skeleton.json
anim_converter.exe \
              --input "../Assets/Plugins/Animancer/Examples/Art/Spider Bot/SpiderBot-MoveUp.anim" \
              --skeleton build/spider_skeleton.json --output build/spider.ab
bgfx_demo.exe --model "../Assets/Plugins/Animancer/Examples/Art/Spider Bot/SpiderBot.FBX" \
              --clip build/spider.ab --camera-yaw 25 --frames 12 \
              --fixed-dt 0.0333333 --screenshot shot --screenshot-frame 6
```

- `--screenshot-frame` 控制截图帧（默认 10），`--camera-yaw` 控制环绕 Y 轴视角（默认 0，
  即 +Z 前视；25 为前 3/4；90 为侧视）。截图回调写 PPM（`bgfx::requestScreenShot`）。
- `--fixed-dt` 固定每帧模拟步长；非交互截图验收建议使用，避免高帧率机器上
  `--screenshot-frame` 只采样到动画开头（例如 30 fps 用 `0.0333333`，60 fps 用 `0.0166667`）。
- 无蒙皮/无权重 mesh：眼部等顶点绑定到其自身节点骨（inverseBind = identity），保持节点局部坐标；
  普通蒙皮顶点仍使用 `mOffsetMatrix` 的镜像/缩放版本。
- 手性：FBX 原始 X 轴在导入时镜像到 Unity 运行时坐标，骨架、顶点和 inverseBind 一次性同步转换；
  不要每帧转换。
- Blender/glTF 资产路径见 [18](18-Blender资产管线与运行时补齐.md)。P8.1-P8.5 已实现，
  P8.6 的文档/命令已完成；支持 `skeleton.json` 的可选 `inverseBind`、
  `anim_manifest.json` 读取、离线 `anim_pack`（GLB/glTF → `.ab` + skeleton +
  manifest）、Blender Marker sidecar、运行时 `AnimationClipLibrary`/`AnimationTransitions`
  （事件 + rootBone）、headless `anim_pack_demo --manifest`，以及 `HobiAnimator`
  的 fade/stop/isPlaying/layer 门面；Unity `.anim` 转换路径保持不变。

### 11.4 Humanoid retarget 视觉验收

`humanoid_retarget_demo` 与 mesh demo 分离，使用 golden skeleton/avatar 直接绘制两套骨架，
避免 FBX 导入尺度/坐标问题干扰 retarget 判定。左侧为 source，右侧为 `--scale`（默认 1.35）
缩放后的 target；绿色为脚底 goal，品红为手部 goal，黄色为 actual→goal 连线。

```bash
cmake --build build --config Release --target humanoid_retarget_demo
build/samples/bgfx_demo/Release/humanoid_retarget_demo.exe \
  --clips build/ab_test/Humanoid-Walk.ab,build/ab_test/Humanoid-Run.ab,build/ab_test/Humanoid-Idle.ab \
  --clip-frames 120 --screenshot-frame 30 --screenshot build/retarget_acceptance \
  --shaders build/samples/bgfx_demo/Release/shaders --strict
```

- 默认数据：`tests/golden/DefaultHumanoid.skeleton.json` 与
  `DefaultHumanoid.source.avatar.json`。
- `--screenshot prefix` 为每个 Clip 写 `prefix-<clip>.ppm`；截图 frame 由 `--screenshot-frame`
  控制（默认 30）。
- 每个 Clip 统计整段的最大脚/手端点误差；`--strict` 在超过 `--goal-tolerance`（默认 0.05 m）
  时返回非零。验收记录见 `docs/17` §6.3。

### 11.5 并行与基准

`ThreadPoolJobScheduler` 已可作为 `IJobScheduler` 注入（`ScriptPoseNode` 的 job）。
跨角色并行需要在引擎侧按角色调用 `HobiAnimator::update`，再 `waitAll` 后上传调色板。
本机 Release 实测（Ryzen 级 16 线程，仅动画更新+调色板，不含 GPU）：

| 场景 | 200 角色 × 64 骨 × 300 帧 |
|---|---|
| 串行（优化前） | 7.22 ms/frame |
| 串行（xform SIMD + passthrough + TRS palette） | 1.9–2.0 ms/frame（≈500–533 FPS） |
| 并行（优化前） | 1.01 ms/frame |
| 并行（`parallelFor`） | 0.35–0.47 ms/frame（≈2113–2896 FPS） |

> 该数字只用于回归趋势，不是引擎帧预算承诺；`tools/anim_bench` 可复现。
> 128 骨串行也从约 24.29 ms/frame 降到约 3.9 ms/frame。
>
> **性能优化已完成**：xform float4 + SSE2 是当前主路径，不再以跨骨 SoA 为必做项。
> 后续仅保留单图内 Job 按链并行、曲线采样 profile 等可选精确模式（性能优化，见 [17](17-功能范围与验收.md) §5.4）。

### 11.6 Blender/glTF 同骨架视觉验收（P8.6）

`anim_pack` 的输出必须先通过 headless 验收，再用同一 Blender rig 的模型做 bgfx 截图：

```powershell
# 先按 docs/18 §8.3 用 Blender 导出 events.json
cmake -S . -B build_pack -DANIM_BUILD_ANIM_PACK=ON
cmake --build build_pack --config Release --target anim_pack anim_pack_demo
build_pack/tools/anim_pack/Release/anim_pack.exe `
  --input character.glb `
  --out-dir build_pack/anim_out `
  --manifest anim_manifest.json `
  --events build_pack/anim_out/events.json
build_pack/tools/anim_pack/Release/anim_pack_demo.exe `
  --manifest build_pack/anim_out/anim_manifest.json `
  --clip-name Walk --seconds 2 --samples 8

cmake --build build --config Release --target bgfx_demo
build/samples/bgfx_demo/Release/bgfx_demo.exe `
  --model character.fbx `
  --clip build_pack/anim_out/Walk.ab `
  --shaders build/samples/bgfx_demo/Release/shaders `
  --frames 65 --fixed-dt 0.0333333 `
  --camera-yaw 25 --screenshot build/blender_walk --screenshot-frame 30
```

约束：`bgfx_demo` 直接按模型导入顺序解释 `.ab` 的 `boneIndex`，模型的骨骼顺序必须与
`character.skeleton.json` 一致；不同骨架应走 P9 的 `RetargetClipTransition`（源
`BoneLocalClip` + 源/目标 `HumanoidAvatar`），而不是把错误顺序静默播放。
当前仓库只有合成 `simple_rig.glb`，真实 Blender 截图仍需外部 `.blend`/`.glb` 资产。

