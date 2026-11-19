# 16 · 任务调度与 ECS 集成（Animation Jobs）

本章专门回答“Animation Jobs 应该用 ECS 的 Job System 实现吗？是否用 EnTT？”并给出可落地的架构与适配方案。

---

## 1. 概念澄清（先厘清两个维度）

Unity 的 **Animation Jobs** 指的是：
- `IAnimationJob`（`ProcessRootMotion(AnimationStream)` / `ProcessAnimation(AnimationStream)`）。
- 通过 `AnimationScriptPlayable` 插入图，作用对象是**每个角色的 `AnimationStream`（位姿）**。
- 底层执行用的是 **C# Job System + Burst + NativeArray**。

它包含两个独立维度：

| 维度 | 含义 | 典型技术 |
|------|------|----------|
| **任务调度（Job System）** | 把工作拆成任务，放到线程池并行执行，带依赖与完成等待 | Unity C# Job System、Taskflow、enkiTS、oneTBB、引擎自带调度器 |
| **ECS（实体-组件-系统）** | 数据按组件存储，系统按视图批量遍历，数据导向 | EnTT、Unity DOTS、flecs |

**关键结论：EnTT 是 ECS，不是并行的 Job System。** EnTT 提供 `entt::registry`（实体/组件）、`view/group`（批量遍历）、`dispatcher`（事件）、`organizer/process`（协作式流程编排），但**不提供线程池、并行 for、任务依赖调度**。因此：
- 不能“用 EnTT 来做 job system”的并行部分；
- 但可以用 EnTT 来做“动画数据的 ECS 存储与系统遍历”，并行部分另接一个 Job Scheduler。

## 2. 推荐架构（决策）

```
              ┌──────────────────────────────────────────────┐
              │  anim 核心（依赖仅 glm，ECS 无关）            │
              │   IAnimationJob  +  IJobScheduler 抽象        │
              └───────────────▲──────────────────────────────┘
                              │ 适配
        ┌─────────────────────┼─────────────────────────┐
        │                     │                         │
┌───────────────┐   ┌───────────────────┐   ┌────────────────────┐
│ 线程池/任务图  │   │ EnTT ECS 适配（可选）│   │ 引擎自带 ECS/Job   │
│ enkiTS/Taskflow│   │ components+systems │   │ SystemGroup 适配   │
│ 或自研         │   │ + IJobScheduler    │   │                    │
└───────────────┘   └───────────────────┘   └────────────────────┘
```

决策：
1. **核心不绑定 ECS**：`IAnimationJob` 与 `IJobScheduler` 是普通 C++ 接口；`anim` 静态库仅依赖 glm。
2. **是否用 EnTT：可用，但仅作 ECS 适配层。**
   - 若你的引擎已经是 EnTT ECS → 用 EnTT 存动画组件、用 `view` 驱动系统；**并行仍由 `IJobScheduler` 执行**。
   - 若引擎不用 ECS → 不用 EnTT，直接 `IJobScheduler` + 自管 `std::vector<HobiAnimator*>`。
   - EnTT 作为**可选依赖**（编译开关 `ANIM_ENABLE_ENTT`），不进入核心库。
3. **并行由 Job Scheduler 负责**：默认提供“单线程后端”，可替换为 enkiTS/Taskflow/引擎自带。
4. **跨角色并行是主收益**：角色之间无依赖；单个角色内部有依赖（采样→混合→IK→调色板），用依赖图串起来。

## 3. Job 模型（核心接口）

### 3.1 IAnimationJob（等价 Unity `IAnimationJob`）

```cpp
// include/anim/jobs/IAnimationJob.h
struct IAnimationJob {
    virtual ~IAnimationJob() = default;
    // 对应 ProcessRootMotion；默认空
    virtual void processRootMotion(Pose& pose, const Skeleton& sk) {}
    // 对应 ProcessAnimation；deltaTime 供 Damping/Lean 使用
    virtual void processAnimation(Pose& pose, const Skeleton& sk, float deltaTime) = 0;
};
```

- 数据即“Pose 的读/写”；不再需要 Unity 的 `TransformStreamHandle`（我们拥有 Pose）。
- job 作为 `ScriptPoseNode` 的载荷（见 [06 §5](06-Playable图.md)、[10 §4](10-根运动与Animation-Jobs.md)）。

### 3.2 IJobScheduler（任务调度抽象）

```cpp
// include/anim/jobs/JobScheduler.h
struct JobHandle { uint64_t id = 0; bool valid() const; };

class IJobScheduler {
public:
    virtual ~IJobScheduler() = default;

    // 提交一个任务；deps 表示依赖哪些前置任务
    virtual JobHandle schedule(std::function<void()> work,
                               std::span<const JobHandle> deps = {}) = 0;

    // 等待单个/全部完成
    virtual void complete(JobHandle) = 0;
    virtual void waitAll() = 0;

    // 并行 for（可选，用于骨骼区间采样/混合）
    virtual void parallelFor(size_t count,
                             std::function<void(size_t begin, size_t end)> body) = 0;

    virtual int workerCount() const = 0;
};

// 内置单线程后端（默认，行为等价原项目单线程）
class InlineJobScheduler : public IJobScheduler { /* 立即执行 */ };
```

- 提供 `InlineJobScheduler`（同步执行）与“可替换后端”：
  - `EnkiTSScheduler` / `TaskflowScheduler`：可选精确模式（性能优化），不阻塞当前发布。
  - `EngineSchedulerAdapter`：包装你引擎的 job system。
- **不**把线程池塞进核心；核心只依赖 `IJobScheduler` 接口。
- P7 已实现 `ThreadPoolJobScheduler`（`include/anim/jobs/ThreadPoolJobScheduler.h`）：
  - 依赖计数：任务只在所有前置完成后入队；`waitAll` 等待未完成计数归零。
  - `parallelFor(count, body)` 按 worker 数分片；适合跨角色更新。
  - 跨角色并行示例见 `tools/anim_bench --parallel`（200×64 骨约 0.35–0.47 ms/frame）。

## 4. ECS 数据模型（EnTT 适配，可选）

若引擎使用 EnTT，则每个角色是一个 entity，动画数据以组件存储：

```cpp
// include/anim/integration/ecs/entt/Components.h  (仅当 ANIM_ENABLE_ENTT)
struct HobiComponent   { HobiAnimator* animator = nullptr;
                              std::vector<PlayableHandle> jobHandles; };
struct JobListComponent     { std::vector<IAnimationJob*> jobs; };
struct PoseComponent        { Pose pose; };                 // 可选副本
struct SkinningComponent    { std::vector<Mat4> palette; }; // 可选副本
struct RootMotionComponent  { RootMotionDelta delta; };     // 可选副本
```

系统（`Systems.h`，header-only）：

```cpp
// 把 JobListComponent 的 job 插入图输出链；重复调用会先移除旧 job。
void bindAnimationJobs(entt::registry& reg);

// 每角色图求值 + job（job 已在输出链中，顺序执行）+ computePalette。
void animationGraphSystem(entt::registry& reg, float dt);
void animationGraphSystem(entt::registry& reg, IJobScheduler& sched, float dt); // 跨角色并行

// 可选：把 Pose/palette/rootMotion 同步到组件，供渲染/其他系统读取。
void syncAnimationOutputs(entt::registry& reg);
```

流水线与调度：
```
bindAnimationJobs（角色创建后一次）
animationGraphSystem  ──▶ syncAnimationOutputs
```
- 跨实体可并行；实体内部 job 按插入顺序执行。
- EnTT 只负责组件存储与 `view` 遍历；并行仍由外部 `IJobScheduler` 驱动。
- EnTT 的 `entt::organizer` 可用于声明系统顺序（但要自己驱动），不作为并行调度器。

## 5. 执行流水线（每帧）

```
for each entity (parallel):
    graph.evaluate(dt)                // 双阶段更新 + 图求值 -> Pose
    for job in JobList:               // IK/Damping/Lean...
        job.processAnimation(pose, skeleton, dt)
    extractRootMotion(prev, curr)
    computePalette(skeleton, pose, palette)
sync (IJobScheduler::waitAll)
upload palettes -> bgfx
```

时序要求（与 Unity 一致）：
- 事件（EventDispatcher）在 post-update 阶段、**在所有图求值之后**执行（[07 §9](07-Hobi门面.md)）。
- IK/调色板在事件之后、渲染上传之前。

## 6. 并行策略

| 粒度 | 是否并行 | 说明 |
|------|----------|------|
| 跨角色 | ✅ 主收益 | 角色彼此独立 |
| 同角色的采样/混合（按骨区间） | ✅ 可选 | `parallelFor` 按骨范围切分 |
| 同角色的 Mixer 子姿态采样 | ✅ 可选 | 子状态相互独立 |
| 同角色的 IK 链 | ❌ 串行 | 父子依赖 |
| 调色板计算 | ✅ 可选 | 每骨独立（父链求世界矩阵需顺序，可两趟） |

注意：
- 避免过度并行导致线程争用；角色数少时用 `InlineJobScheduler` 反而更快。
- `Incremental`/确定性：并行混合的累加顺序若不固定会有浮点差异；若需确定性，固定归约顺序或串行混合。

## 7. EnTT 依赖策略

- 仅 `integration/ecs/entt` 头文件引用 EnTT，通过 `ANIM_ENABLE_ENTT` 控制；
  核心库 `anim` 不链接 EnTT。
- 引入方式：CMake 设置 `-DANIM_ENABLE_ENTT=ON -DANIM_ENTT_DIR=<entt根目录>`
  （自动查找 `src/entt/entt.hpp` / `single_include/entt/entt.hpp`）。
- 若引擎已有 EnTT，用同一版本避免 ODR/ABI 冲突。
- 若不用 EnTT：完全不需要该依赖，主构建默认关闭。

## 8. 与 Unity 的对照

| Unity | 本库 |
|-------|------|
| `IAnimationJob` | `IAnimationJob` |
| `AnimationScriptPlayable` + 调度 | `ScriptPoseNode` + `IJobScheduler` |
| `AnimationStream` / `TransformStreamHandle` | `Pose` / `Skeleton` 直接访问 |
| `NativeArray` | `std::span` / `std::vector`（池化） |
| C# Job System | `IJobScheduler`（可接 enkiTS/Taskflow/引擎） |
| Burst | 已实现热点 SIMD：`Transform` 3×float4 + SSE2 pose 混合；`computePalette` 走世界 TRS |
| ECS（DOTS） | 可选：EnTT 适配（组件/系统） |

## 9. 何时不需要 EnTT

- 引擎非 ECS：用 `std::vector<HobiAnimator*>` + `IJobScheduler` 即可。
- 角色数少、以逻辑为主：单线程 `InlineJobScheduler`。
- 只要 IK/根运动、不追求大规模并行：不引入 ECS。

## 10. 文件清单

```
include/anim/jobs/IAnimationJob.h
include/anim/jobs/JobScheduler.h          // IJobScheduler / JobHandle / InlineJobScheduler
include/anim/jobs/ThreadPoolJobScheduler.h // 依赖计数 + parallelFor（P7）
include/anim/jobs/ScriptPoseNode.h        // 图节点适配（见 06）
include/anim/jobs/TwoBoneIK.h
include/anim/jobs/Damping.h
include/anim/jobs/SimpleLean.h
include/anim/jobs/AnimatedValue.h
include/anim/integration/ecs/entt/Components.h   // 可选
include/anim/integration/ecs/entt/Systems.h      // 可选
include/anim/integration/ecs/entt/Adapters.h     // 可选
```

## 11. 验收

1. `InlineJobScheduler` 下，所有 `IAnimationJob` 结果与 Unity Job 样例一致（见 [13](13-测试与黄金数据.md)）。
2. 替换为并行后端后，结果在容差内一致（确定性场景按固定归约验证）。
3. EnTT 适配：多角色 entity 能独立播放/IK/蒙皮，无串扰。
4. 核心库 `anim` 在关闭 `ANIM_ENABLE_ENTT` 时可独立编译与测试。
