# 10 · 根运动与 Animation Jobs

本章包含两部分：**根运动重定向** 与 **过程式动画（Animation Jobs / IK）**。

> 关于 “Animation Jobs 是 ECS 的 Job System 吗 / 是否用 EnTT”，见专门的 [16 · 任务调度与 ECS 集成](16-任务调度与ECS集成.md)。本章只讲算法与接口，不绑定具体调度后端。

---

## 第一部分：根运动

原项目：`Utilities/Redirect Root Motion/*`。核心组件 `RedirectRootMotion<T>`（`RedirectRootMotion.cs`）把 `Animator` 的根运动转给另一个对象：
```csharp
// RedirectRootMotionToTransform.cs:25-26
Target.position += Animator.deltaPosition;
Target.rotation *= Animator.deltaRotation;
```
开关由 `Animator.applyRootMotion` 控制（`RedirectRootMotion.cs:45-48`）。

### 1. 本库设计

C++ 引擎没有 `Animator`，根运动由 L3 `extractRootMotion`（见 [04](04-骨架与位姿.md)）产出 `RootMotionDelta`，再由“重定向目标”消费。

```cpp
struct RootMotionDelta { Vec3 deltaPosition; Quat deltaRotation; bool hasMotion; };

class IRootMotionTarget {
public:
    virtual ~IRootMotionTarget() = default;
    virtual void applyRootMotion(const RootMotionDelta&) = 0;
    virtual bool enabled() const { return true; }
};

class RootMotionRedirect {
public:
    IRootMotionTarget* target;
    bool applyRootMotion = true;   // ≈ Animator.applyRootMotion
    void update(const RootMotionDelta& d) { if (applyRootMotion && target && target->enabled()) target->applyRootMotion(d); }
};
```

内置实现（对应原三个子类）：
- `RootMotionToTransform`：`position += d.deltaPosition; rotation *= d.deltaRotation;`（等价 `RedirectRootMotionToTransform`）。
- `RootMotionToCharacterController` / `RootMotionToRigidbody`：在你的引擎中对应“角色控制器/刚体”接口，由集成层提供适配；算法同为“应用位移/旋转（或速度）”。

### 2. 提取位置

- **通用 Clip**：Hips（或模型根骨）的局部 TRS 在相邻帧的变化。
- **人形 Clip**：`RootT/RootQ` 已是根空间位移/旋转；应用/提取时按 `humanScale` 缩放平移（见 05 §4）。
- 是否剥离 Y（垂直）分量由引擎/角色决定（`RootMotionDelta` 提供原始值，交给 target 决策）。

### 3. 开关与 IK 标志

- 原项目用 `ApplyAnimatorIK` / `ApplyFootIK`（`AnimancerNode.cs:901-961`）把 IK 开关从层/状态向下传播。
- 本库保留同名标志：只有当状态（及其父链）的 `ApplyFootIK` 为真时才应用 `LeftFootT/Q` 等 IK goal；`ApplyAnimatorIK` 控制过程式 IK 节点。

---

## 第二部分：Animation Jobs / IK

### 4. 概念与执行模型

Unity 把 Unity 的 `IAnimationJob` + `AnimationScriptPlayable` 作为扩展点：job 读写 `AnimationStream`（位姿）。本库等价设计：
- **任务内容**：`IAnimationJob`（读写 `Pose`）。
- **任务执行**：由 `IJobScheduler` 调度（默认单线程，可换并行后端）。
- **图位置**：`ScriptPoseNode`，位于“混合后、输出前”（等价 `AnimationScriptPlayable`）。

> **不把 EnTT 当作 Job System**：EnTT 是 ECS（组件/遍历），没有线程池。是否用 EnTT 仅影响“数据如何组织（组件/系统）”，不影响 job 如何执行。详见 [16](16-任务调度与ECS集成.md)。

### 5. IAnimationJob 与 ScriptPoseNode

```cpp
// include/anim/jobs/IAnimationJob.h
struct IAnimationJob {
    virtual ~IAnimationJob() = default;
    virtual void processRootMotion(Pose& pose, const Skeleton& sk) {}
    virtual void processAnimation(Pose& pose, const Skeleton& sk, float deltaTime) = 0;
};

// include/anim/jobs/ScriptPoseNode.h
class ScriptPoseNode : public PlayableNode {   // 等价 AnimationScriptPlayable
public:
    IAnimationJob* job = nullptr;
    void processFrame(float dt) override;      // 拉取 input[0] 得 Pose，再执行 job
};
```

插入/移除：`HobiPlayable::insertOutputJob(job)` / `removePlayable(node)`（对齐原 `InsertOutputJob` / `AnimancerUtilities.RemovePlayable`，`AnimancerPlayable.cs:513-535`）。

### 6. 内置 Job

- `AnimancerJob<T>`（原 `AnimancerJob.cs`）：把 job 插在根与输出之间的基类 → 本库用 `ScriptPoseNode` + `insertOutputJob` 表达。
- `AnimatedFloat/Bool/Int`（原 `Utilities/Animation Jobs/*`）：从“属性通道”读取被动画驱动的值（见 §8）。
- **TwoBoneIK / Damping / Lean**：原项目在 `Examples/10 Animation Jobs/*`，算法移植如下。

### 6.1 TwoBoneIK（移植 `TwoBoneIKJob.cs`）

输入：`top/mid/low` 三根骨索引 + effector（目标位置/旋转）。
```
aPosition/bPosition/cPosition = 世界位置(top/mid/low)
ePosition = effector 世界位置
ab=b-a; bc=c-b; ac=c-a; ae=e-a
abcAngle = TriangleAngle(|ac|, ab, bc)
abeAngle = TriangleAngle(|ae|, ab, bc)
angle = (abcAngle - abeAngle) * rad2deg
axis = normalize(cross(ab, bc))
midWorldQ = AngleAxis(angle, axis) * midWorldRot
cPosition = 更新后 low 世界位置; ac = c - a
topWorldQ = FromToRotation(ac, ae) * topWorldRot
lowWorldQ = effector 旋转
```
其中 `TriangleAngle(aLen,v1,v2) = acos(clamp((|v1|²+|v2|²-aLen²)/(|v1||v2|)/2,-1,1))`（`TwoBoneIKJob.cs:46-52`）。
- 世界旋转→局部旋转由 `Skeleton`/`Pose` 的父链换算（Unity 用 stream handle）。
- 共线链 `cross(ab,bc)==0` 时 elbow 旋转取 identity，但仍继续写 top/low；
  low 始终写 effector rotation，与源码后续步骤一致。

`applyHumanoidGoals()` 的 humanoid 路径使用 Mecanim 源式 `Skeleton2BoneIK`，其权重语义与
`FullBodySolve` 一致：raw goal 直接传给求解器，权重在肘/肩旋转混合时才应用；
只有 `Skeleton2BoneAdjustLength` 使用 `lerp(当前末端, raw goal, WeightT)` 作为拉伸距离。
源式求解保留 `q=(axis*sin(a),cos(a))` 的半角四元数构造，并显式处理 glm 的 `(w,x,y,z)` 顺序。
末端 rotation 权重同样使用最短路径 nlerp（对应 Unity `quatLerp`），与位置权重分开。
Axes 有效时，先按源码对末端执行 `AddAxis → quatLerp → RemoveAxis`，再执行
`HumanFixEndDoF(mid, end)`：用 parent/child 的 `FromAxes(0)` 重分配局部旋转、
对 parent 做 `SkeletonAlign`，最后恢复 end 原 world rotation。
没有 Axes 数据时，`SkeletonGetDoF`/`SkeletonSetDoF` 按源码
无 Axes 分支回退到 `quat2Qtan`/`qtan2Quat`，并在求解前应用
`DoF * (1, 0, 0.9, 0)` 的 mid 骨预调整。若 Avatar 从 source avatar 载入
`axes`（`preQ/postQ/sign/limits/type`），则走 `ToAxes`/`FromAxes` 的
`kZYRoll` 路径。

### 6.2 Damping（移植 `DampingJob.cs`）

1. `computeDampedPositions`：从根开始，`newPos = parentPos + parentRot*localPos[i]`；`SmoothDamp(cur,newPos,vel,0.15,∞,dt)`；约束边长 `parentPos + normalize(newPos-parentPos)*|localPos[i]|`（`DampingJob.cs:72-103`）。
2. `computeJointLocalRotations`：由新旧方向求 `FromToRotation`，更新全局旋转再转局部（`:118-145`）。
- 零长度段方向退化为 0 时仍写入 local rotation 并推进 `parentRotation`，与源码一致。
- `SmoothDamp` 用标准临界阻尼实现（可移植 Unity 公式）。

### 6.3 Lean（移植 `SimpleLean`）

按给定方向/角度对脊柱链施加侧倾：对每根参与骨做绕前向轴的角度插值，分摊到多根骨（示例 `SimpleLean.cs`）。本库实现为 `SimpleLeanJob`。

### 6.4 TwistSolve（移植 `HumanFixTwist`）

`applyHumanoidTwist()` 对应 `human.cpp:2352` 的 `TwistSolve`：
- 按 Unity Human 默认值 `ArmTwist/ForeArmTwist/UpperLegTwist/LegTwist = 0.5`。
- 对 `UpperArm→LowerArm`、`LowerArm→Hand`、`UpperLeg→LowerLeg`、`LowerLeg→Foot` 八对骨。
- Avatar 带 Axes 时使用 `ToAxes/FromAxes` 并通过 `SkeletonAlign` 对齐 post 轴；
  无 Axes 时回退到 `qtan`：缩放 x（twist）分量后转回四元数。
- 随后恢复子骨原来的世界旋转并反算局部旋转，保持末端朝向不变。

### 6.5 LookAt（移植 `FullBodySolve` 的 look-at 段）

`applyHumanoidLookAt()` 接收世界空间目标与 `(clamp, body, head, eyes)` 权重：
- clamp：按 `quatClamp` 限制头部目标相对当前眼睛 forward 的最大角度。
- body：先旋转 Spine（有 Chest 时权重减半），再旋转 Chest。
- head：旋转 Neck（权重 `head * 0.5`），再对 Head 迭代 3 次（权重 `head²`）。
- eyes：对 LeftEye/RightEye 分别旋转。
- 无 Axes 时用 Head 的世界 +Y 作为眼睛 forward，并用 `worldPosition/worldRotation`
  重算父链；这是回退近似，不包含 `AddAxis` 的 postQ 修正。

### 7. 与调度后端的关系

- `IAnimationJob` 只描述“算什么”，不关心“在哪跑”。
- `IJobScheduler`（见 [16](16-任务调度与ECS集成.md)）负责串行/并行、依赖、完成。
- 同一角色的多个 job 按 `JobList` 顺序执行（IK 链有依赖）；不同角色之间可并行。
- 若引擎是 EnTT ECS：`PoseComponent + JobListComponent` 由系统批量遍历，但 job 的实际并行仍交给 `IJobScheduler`。

### 8. Animated 属性（移植 `AnimatedFloat/Bool/Int`）

原项目从动画流读取“被动画驱动的属性”（`AnimatedFloat.cs:52-64`）。本库方案：
- 转换器把 `.anim` 中非骨骼 `m_FloatCurves` 抽取为**命名浮点通道**存入 Clip（见 11），
  `.ab` v3 持久化 `pathHash + attributeHash + curve`。
- `AnimatedFloat/Bool/Int` 支持 `bindProperty(clip, path, attribute)`（path 可省略），
  用 `sample(state.time())` 读取；`bool/int` 用阈值/取整。对应原项目按
  `PropertyStreamHandle` 读流的方式，但不依赖 Unity 的绑定系统。
- 若引擎不需要“动画驱动材质/脚本属性”，可降级为可选。

### 9. 与 Unity 的差异

Unity 的 `AnimationStream` 需要 `TransformStreamHandle`/`PropertyStreamHandle` 绑定；本库因为**拥有** Pose 与 Skeleton，可直接按下标/id 访问，语义更简单但算法等价。绑定层用本库的索引/id 替代。

### 10. 文件清单

```
include/anim/jobs/IAnimationJob.h
include/anim/jobs/JobScheduler.h          // IJobScheduler / JobHandle / InlineJobScheduler
include/anim/jobs/ScriptPoseNode.h
include/anim/jobs/TwoBoneIK.h
include/anim/jobs/Damping.h
include/anim/jobs/SimpleLean.h
include/anim/jobs/AnimatedValue.h
include/anim/hobi/rootmotion/RootMotion.h
include/anim/hobi/rootmotion/Targets.h
```

### 11. 对照校验点

1. TwoBoneIK 的三角几何与旋转组合顺序（§6.1）。
2. Damping 的 SmoothDamp 参数（0.15）与边长约束（§6.2）。
3. IK 开关沿父链传播（`ApplyFootIK/ApplyAnimatorIK`）。
4. 根运动缩放（人形 `humanScale`）。
5. 调度后端切换后结果在容差内一致（串行 vs 并行）。

## 12. 实现状态（P5）

### 12.1 根运动

- `include/anim/hobi/rootmotion/RootMotion.h`
  - `IRootMotionTarget`（`applyRootMotion` + `enabled`）。
  - `RootMotionRedirect`（`applyRootMotion` 开关，对齐 `RedirectRootMotion.ApplyRootMotion`）。
- `include/anim/hobi/rootmotion/Targets.h`
  - `RootMotionToTransform`：`position += deltaPosition; rotation *= deltaRotation`
    （对齐 `RedirectRootMotionToTransform.cs:25-26`）。
  - `RootMotionToCharacterController` / `RootMotionToRigidbody`
    （通过 `ICharacterControllerLike` / `IRigidbodyLike` 适配器）。
- `HobiPlayable`：
  - `setRootMotionTarget()` / `rootMotionTarget()` / `rootMotionDelta()`；
  - `setRootMotionRootBone()`；
  - `evaluate()` 在 `PostUpdate` 后用上一帧 `Pose` 调 `extractRootMotion()`，
    有目标且 `enabled()` 时立即应用（首帧只记录，不应用）。

### 12.2 Jobs / IK

- `include/anim/jobs/JobScheduler.h`：`JobHandle` / `IJobScheduler` / `InlineJobScheduler`。
  - `InlineJobScheduler` 同步执行 `schedule`，`waitAll/complete` 为空操作。
- `ScriptPoseNode::processFrame`：
  - 有 `scheduler` 时 `schedule(...)` + `complete(...)`，否则直接执行；
  - **同步点**保证同一图的后续节点能读到 job 结果。
- `include/anim/jobs/TwoBoneIK.h` + `src/jobs/TwoBoneIK.cpp`：
  - 严格移植 `TwoBoneIKJob.cs:46-86`（三角角度 → 肘部 → 肩部 → 末端旋转）。
- `include/anim/avatar/HumanoidIK.h` + `src/avatar/HumanoidIK.cpp`：
  - raw humanoid goal → 源码式两骨几何求解的可选桥接；显式 `HumanoidGoalWeights`，
    超出链长时按 `Skeleton2BoneAdjustLength` 做最大 5% 拉伸；不含 Unity
    `FullBodySolve` 的 look-at/twist 全流程。
- `include/anim/jobs/Damping.h` + `src/jobs/Damping.cpp`：
  - 移植 `DampingJob.cs:72-145`；`SmoothDamp` 用 Unity `Mathf.SmoothDamp` 公式。
  - `setup()` 从当前 `Pose` 捕获局部位置/旋转与世界位置。
- `include/anim/jobs/SimpleLean.h` + `src/jobs/SimpleLean.cpp`：
  - 移植 `SimpleLean.Job`：总角度平均分摊，`offset * worldRotation`。
- `include/anim/jobs/AnimatedValue.h`：`AnimatedFloat/Bool/Int` 读 `Curve` 或 `PropertyChannel`。
- `HobiPlayable::insertOutputJob(job, scheduler)`：
  - 对齐 `AnimancerPlayable.cs:527-535`：用 `ScriptPoseNode` 接在当前输出之后；
  - 可重复调用（后插入者成为最终输出）。
- `HobiPlayable::removePlayable(handle)`：
  - 对齐 `AnimancerUtilities.RemovePlayable`：把节点输入重新接到所有父节点；
  - 若节点是图输出，则先断开再恢复原输入为输出。

### 12.3 已接受边界

- **Unity oracle 可选**：TwoBoneIK/Damping 以**几何性质**验收（末端到达目标、
  骨长保持、收敛、Lean 角度分摊）；逐帧 Unity 对拍属于可选精确模式，不作为
  发布阻塞（见 [17](17-功能范围与验收.md)）。
- **并行后端**：`InlineJobScheduler` 仍为默认；`ThreadPoolJobScheduler` 已接入，
  enkiTS/Taskflow/引擎 JobSystem 属于可替换后端，不改变当前功能语义。
- **根运动时序**：根运动在动画 Job 之后提取（含 Job 对根骨的影响）。这是本库的
  **最终时序**，不再作为待调整项；引擎侧按该顺序消费根运动与 `RootMotionDelta`。

