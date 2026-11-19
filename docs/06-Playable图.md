# 06 · Playable 图

L5 层：自实现一个与 Unity Playables **语义等价**的求值图。原项目大量依赖 `PlayableGraph/Playable/PlayableBehaviour`（见 `AnimancerPlayable.cs`、`AnimancerNode.cs`、`AnimancerLayer.cs` 的 `_Graph.Connect/Disconnect/SetInputWeight/SetInputCount`）。本库要用 C++ 提供同名语义，使上层 Animancer 门面可以“照抄原逻辑”。

## 1. 设计目标

1. 支持有向无环图（DAG），节点有 N 个输入口，每口带权重。
2. 支持运行时 `connect/disconnect/setInputCount/setInputWeight`（原项目频繁动态改图）。
3. 支持两阶段更新：`prepareFrame(dt)`（早于子节点求值）+ `processFrame(dt)`（求值）。
4. 支持时间/速度/播放暂停。
5. 句柄安全：用 `{index,version}` 句柄，删除后版本自增，避免悬空引用（对齐 Unity `Playable` handle 语义）。
6. 与“位姿/数据”解耦：节点通过虚函数产出任意类型数据；动画相关节点产出 `Pose`。

## 2. 句柄与存储

`include/anim/graph/PlayableHandle.h`

```cpp
struct PlayableHandle {
    uint32_t index = 0xFFFFFFFF;
    uint32_t version = 0;
    bool isValid() const { return index != 0xFFFFFFFF; }
    bool operator==(const PlayableHandle&) const = default;
};
```

`PlayableGraph` 内部用“槽位数组 + 空闲栈 + 版本号”存储节点（deferred 删除可选，先做直接删除 + 版本校验）。

## 3. 节点基类

`include/anim/graph/PlayableNode.h`

```cpp
class PlayableNode {
public:
    virtual ~PlayableNode() = default;

    // 生命周期（对齐 Unity PlayableBehaviour）
    virtual void onPlayableCreate(PlayableGraph&, PlayableHandle self) {}
    virtual void onPlayableDestroy() {}

    // 两阶段更新
    virtual void prepareFrame(float deltaTime) {}          // 早于子节点求值
    virtual void processFrame(float deltaTime) {}          // 求值（拉取子节点）

    // 时间/速度/播放
    float    speed() const { return _speed; }
    void     setSpeed(float s) { _speed = s; }
    double   time() const { return _time; }
    void     setTime(double t) { _time = t; }
    void     setDuration(double d) { _duration = d; }
    bool     isPlaying() const { return _playing; }
    void     play()  { _playing = true; }
    void     pause() { _playing = false; }

    // 输入口管理（由 Graph 调用）
    int  inputCount() const;
    PlayableHandle input(int i) const;
    void setInput(int i, PlayableHandle);

protected:
    PlayableGraph* _graph = nullptr;
    std::vector<PlayableHandle> _inputs;
    std::vector<float> _inputWeights;
    float  _speed = 1.0f;
    double _time = 0, _duration = 0;
    bool   _playing = true;
};
```

## 4. 图

`include/anim/graph/PlayableGraph.h`

```cpp
class PlayableGraph {
public:
    template <typename T, typename... Args>
    PlayableHandle create(Args&&...);                 // 创建并 onPlayableCreate
    void destroy(PlayableHandle);                     // 递归销毁子图
    void destroySubgraph(PlayableHandle);             // 等价原 LayerList.Count 递减用法

    void connect(PlayableHandle src, int srcPort,
                 PlayableHandle dst, int dstPort);
    void disconnect(PlayableHandle dst, int dstPort);
    void setInputWeight(PlayableHandle dst, int port, float w);
    float getInputWeight(PlayableHandle dst, int port) const;
    void setInputCount(PlayableHandle node, int count);
    int  getInputCount(PlayableHandle node) const;

    // 输出
    void setOutput(PlayableHandle root);
    PlayableNode* get(PlayableHandle) const;

    // 求值
    void play();
    void stop();
    void evaluate(float deltaTime);
    bool isValid() const;
};
```

### 4.1 求值顺序（关键）

`evaluate(dt)`：
1. 从输出根节点出发，按**拓扑序**（父→子）调用 `prepareFrame(dt)`（保证早于任何子节点求值；对应原项目 `AnimancerPlayable.PrepareFrame` 与 `PostUpdate.PrepareFrame` 的语义）。
2. 再从根节点 `processFrame(dt)` 触发下拉求值：节点在 `processFrame` 内部对每个输入调用 `child->processFrame(dt)`（或由内部求值器统一调度），合成输出。
3. 时间推进：节点在 `processFrame` 中按 `_playing` 与 `_speed` 推进 `_time`（或用外部统一时钟；见 §6）。

> 原项目依赖 Unity 自动评估；本库自行实现等价顺序。注意 `PostUpdate` 节点必须排在其“后续阶段”，原项目通过把它接到 root 的**第 2 输入口**并单独连接/断开实现（`AnimancerPlayable.cs:1357`），从而保证它最后被 `prepareFrame`。本库照搬：`ScriptNode` 有 `inputCount=2`，input0=LayerMixer，input1=PostUpdate。

## 5. 内置节点类型

| 节点 | 对应 Unity | 职责 |
|------|-----------|------|
| `ScriptNode` | `ScriptPlayable<T>` / `PlayableBehaviour` | 承载用户行为（如 AnimancerPlayable/PostUpdate） |
| `MixerNode` | `AnimationMixerPlayable` | N 路加权混合 `Pose` |
| `LayerMixerNode` | `AnimationLayerMixerPlayable` | 层间混合 + additive + AvatarMask |
| `ClipNode` | `AnimationClipPlayable` | 采样单个 Clip 产出 Pose |
| `RetargetClipNode` | 无直接对应（P9 可选） | 采样源 BoneLocal Clip，经 HumanoidAvatar 重定向到目标骨架 Pose |
| `ScriptPoseNode` | `AnimationScriptPlayable` | 在 Pose 流上执行自定义 job（IK/过程式）；job 的实际执行可由 `IJobScheduler` 调度（见 [16](16-任务调度与ECS集成.md)） |

```cpp
class MixerNode : public PlayableNode {
public:
    void processFrame(float dt) override;   // 加权累加子 Pose（调用 skeleton::blend）
    const Pose& output() const;
};

class LayerMixerNode : public PlayableNode {
public:
    void setLayerAdditive(uint32_t layer, bool);
    void setLayerMask(uint32_t layer, const AvatarMask*);
    void processFrame(float dt) override;   // 从底到顶覆盖/加法混合
};

class ScriptPoseNode : public PlayableNode {   // ≈ AnimationScriptPlayable
public:
    IAnimationJob* job = nullptr;
    IJobScheduler* scheduler = nullptr;        // 可选：用任务调度器执行 job
    void processFrame(float dt) override;      // 拉取 input[0] 得 Pose，再执行 job
};
```

> 说明：`ScriptPoseNode` 是“图节点”层面的位置；**是否并行执行 job 由 `IJobScheduler` 决定**（默认单线程）。图求值本身不代表任务并行——两个维度分离，详见 [16](16-任务调度与ECS集成.md)。

- `AvatarMask`：本库自己定义（按 bone 索引的权重位掩码），见 07 层章节。
- 层间混合：底向上，非加法层覆盖其影响骨骼；加法层在结果上叠加。对应原理同 `UnityEngine.Animations.AnimationLayerMixerPlayable`（原项目 `LayerList.SetMask/SetAdditive`，`AnimancerPlayable.LayerList.cs:287-319`）。

## 6. 时钟与 deltaTime

- 原项目用 `FrameData.deltaTime * effectiveParentSpeed` 推进（`AnimancerPlayable.cs:1252`）。
- 本库：`AnimancerPlayable` 在早期更新里计算 `DeltaTime`；节点的时间推进由 `prepareFrame` 阶段统一完成（或由 ClipNode/MixerNode 的 processFrame 推进，二选一，文档统一为**在 prepareFrame 阶段推进时间**，processFrame 只读）。
- `FrameID`：每次 evaluate 自增，`AnimancerState::TimeD` 用它缓存（对应原 `AnimancerState.cs:430,480`）。

## 7. 连接/权重语义（务必与上层约定一致）

- 父节点第 `i` 个输入口跑着一个子节点，权重 = 子节点的 `Weight`。
- Layer 的动态增删沿用原策略：删中间状态时把最后一个换到空位（`AnimancerLayer.cs:199-215`），避免整体搬移。
- Mixer 子节点索引必须连续（`ManualMixerState.cs:361`），因此删除时需整体下移（`ManualMixerState.cs:387-409`）。

## 8. 与对象池/生命周期

- 节点创建销毁频率较低（状态缓存复用），但 `weightless` 克隆（07）会临时新增 State→ClipNode，需复用池。
- `destroy(handle)` 先 `disconnect` 所有引用它的父输入口，再回收。

## 9. 调试

- `dumpGraph()`：按树打印节点类型、输入权重、时间、播放态（对齐原 `AnimancerNode.AppendDescription`）。
- 可选：`GraphVisualizer` 输出 DOT，便于排查连接错误。

## 10. 文件清单

```
include/anim/graph/PlayableHandle.h
include/anim/graph/PlayableNode.h
include/anim/graph/PlayableGraph.h
include/anim/graph/Nodes.h           // ScriptNode/MixerNode/LayerMixerNode/ClipNode/RetargetClipNode/ScriptPoseNode
```

## 11. 与原项目对照

| 原项目 | 本库 |
|--------|------|
| `PlayableGraph.Connect/Disconnect/SetInputCount/SetInputWeight` | `PlayableGraph::connect/disconnect/setInputCount/setInputWeight` |
| `ScriptPlayable<T>.Create` (`AnimancerPlayable.cs:372`) | `graph.create<ScriptNode>(...)` |
| `AnimationMixerPlayable.Create` (`AnimancerLayer.cs:49`, `ManualMixerState.cs:349`) | `graph.create<MixerNode>()` |
| `AnimationLayerMixerPlayable` (`LayerList.cs:58`) | `graph.create<LayerMixerNode>()` |
| `AnimationClipPlayable.Create` (`ClipState.cs:113`) | `graph.create<ClipNode>()` |
| `AnimationScriptPlayable.Create` (`ManualMixerState.cs:726`) | `graph.create<ScriptPoseNode>()` |
| `PrepareFrame` / `PostUpdate.PrepareFrame` | 节点 `prepareFrame` + 根节点驱动 pre/post 列表 |

## 12. P2 实现备注（已完成）

P2 参考了同机 **hdrp-bgfx** 的 RenderGraph 架构（C++20 + bgfx 渲染器）：
`E:\Learn\hdrp-bgfx\src\core\handle.h` 与
`E:\Learn\hdrp-bgfx\src\render\graph\render_graph.{h,cpp}`。
借用其“**代际句柄 + 显式 compile + 错误累积 + 诊断**”的成熟做法，适配到动画图：

| hdrp-bgfx RenderGraph | 本库 PlayableGraph |
|-----------------------|--------------------|
| `Handle<Tag>{index, generation}` | `anim::Handle<Tag>{index, version}`（`core/Handle.h`） |
| `importTexture/createTexture` | `create<T>(...)`（槽位复用 + 版本自增） |
| `addRasterPass + Builder` | `connect/setInputCount/setInputWeight` |
| `compile()`：校验、剔除、排序 | `compile()`：可达性校验、Kahn 拓扑排序、环检测 |
| `lastError()` / `diagnostics()` | 同名 API（首个错误不抛异常） |
| `execute()`：按 pass 顺序录制 | `evaluate()`：prepare（父→子）+ process（子→父） |

实现要点：

1. **句柄**：`PlayableHandle = Handle<PlayableNodeTag>`；销毁时槽位版本自增，
   旧句柄 `get()` 返回 `nullptr`，不会悬空。
2. **存储**：`std::vector<Slot>` + 空闲索引栈；句柄只存 index/version，节点地址稳定。
3. **拓扑**：Kahn 算法（不是简单逆序 DFS），因此 DAG 共享子节点也正确；
   有环时 `compile()` 返回 false，`lastError()` 给出 “cycle”。
4. **两阶段**：
   - `prepare` 按拓扑序（父→子）；
   - `process` 按逆拓扑序（子→父），节点只读取子节点已算好的 `outputPose()`；
   - prepare 期间允许改图（Animancer 的动态连接依赖此行为），改完重新编译。
5. **节点**：`ClipNode / MixerNode / LayerMixerNode / ScriptNode / ScriptPoseNode`。
   - `ClipNode`：构造时取 `Clip.length/looping`，prepare 推进时间，process 采样。
   - `MixerNode`：复用 `blendPoses`，端口权重默认 0（Unity SetInputCount 语义），
     不做自动归一化（由上层保证总和≈1）。
   - `LayerMixerNode`：第 0 个可用输入为基准；覆盖层按 `weight*mask` 逐骨 lerp；
     加法层把输入当作增量（t += p·w，r *= slerp(identity, r, w)，s *= 1+(s-1)·w）。
   - `ScriptNode`：输入 0 的 Pose 直通，prepare/process 回调可注入（PostUpdate 模式）。
     - `ScriptPoseNode`：拷贝输入 Pose 后执行 `IAnimationJob::processAnimation`；
       `IJobScheduler` 已接入：有 scheduler 时 `schedule + complete`，否则直接执行；
       默认 `InlineJobScheduler`，可注入 `ThreadPoolJobScheduler`。
6. **测试**（`test_playable_graph.cpp`、`test_playable_nodes.cpp`）覆盖：
   句柄失效/槽位复用、递归销毁、环检测、拓扑顺序、两输入 PostUpdate、
   Mixer 权重、层覆盖/加法/mask、ScriptPose job、暂停。
