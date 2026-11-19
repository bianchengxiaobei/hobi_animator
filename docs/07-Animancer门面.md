# 07 · Animancer 门面

L6 层：把 L5 的图封装成与 Animancer **同名同语义**的高层 API。目标是让原项目的 C# 逻辑几乎可以“逐行翻译”过来。本章按原文件分节。

> 这是全项目最大的模块。核心类：`AnimancerPlayable` / `AnimancerNode` / `AnimancerState` / `AnimancerLayer` / `LayerList` / `StateDictionary` / `Transition` 体系 / `Fade` / `Events`。

---

## 1. AnimancerPlayable（根）

原项目：`Internal/Core/AnimancerPlayable.cs`。

```cpp
class AnimancerPlayable {
public:
    static AnimancerPlayable* create();           // 建图（对应 Create()）
    static AnimancerPlayable* create(PlayableGraph&);
    void destroyGraph();

    // 输出到骨架
    void createOutput(Skeleton& skeleton);        // 对应 CreateOutput(Animator,...)
    bool destroyOutput();

    // 播放
    AnimancerState* play(const Clip&);
    AnimancerState* play(AnimancerState*);
    AnimancerState* play(AnimancerState*, float fade, FadeMode mode);
    AnimancerState* play(ITransition&);
    AnimancerState* tryPlay(Key);
    void stop(); AnimancerState* stop(Key);
    bool isPlaying(); bool isPlaying(Key); bool isPlayingClip(const Clip&);
    float getTotalWeight();

    // 速度/暂停
    float speed() const; void setSpeed(float);    // 写给 LayerMixer.speed
    bool isGraphPlaying() const; void pauseGraph(); void unpauseGraph();
    void evaluate(); void evaluate(float dt);

    // 更新列表
    void requirePreUpdate(IUpdatable*);
    void requirePostUpdate(IUpdatable*);
    void cancelPreUpdate(IUpdatable*);
    void cancelPostUpdate(IUpdatable*);

    LayerList layers;
    StateDictionary states;
    float defaultFadeDuration = 0.25f;            // 原 DefaultFadeDuration
    static float deltaTime; static uint64_t frameID;   // 帧内全局
};
```

关键语义（照抄原实现）：
- 根节点是 `ScriptNode`（input0 = LayerMixer，input1 = PostUpdate），见 [06](06-Playable图.md)。
  - 原项目：`OnPlayableCreate` 建 `LayerList` 与 `StateDictionary`，`playable.SetInputWeight(0,1)`（`AnimancerPlayable.cs:378-394`）。
- `SkipFirstFade`：Layer0 且当前权重为 0 时跳过淡入（`AnimancerPlayable.cs:303`，`AnimancerLayer.cs:741`）。
- `KeepChildrenConnected`：humanoid 关掉以省性能，generic 打开（`AnimancerPlayable.cs:491-494`）。
- `getKey(clip)`：由组件类型决定 key，默认用 clip 本身（`AnimancerPlayable.cs:676`）。
- 全局 `DeltaTime`/`FrameID`/`Current`：单线程下为静态；多实例或多线程需改为上下文传参（见 12）。

## 2. AnimancerNode（节点基类）

原项目：`Internal/Core/AnimancerNode.cs`。**所有 Weight/Fade/Speed/IK 的通用逻辑都在这里。**

```cpp
class AnimancerNode {
public:
    virtual ~AnimancerNode() = default;

    // 图
    PlayableHandle playable() const;
    virtual void createPlayable();
    void destroyPlayable();
    virtual void recreatePlayable();

    AnimancerPlayable* root() const;
    virtual AnimancerLayer* layer() const = 0;
    virtual AnimancerNode* parent() const = 0;
    int index() const; void setIndex(int);

    // 子节点
    virtual int childCount() const { return 0; }
    virtual AnimancerState* getChild(int) const;    // 默认抛错
    virtual bool keepChildrenConnected() const { return false; }

    // 权重
    float weight() const; void setWeight(float);    // set 同时清 FadeSpeed/TargetWeight
    void setWeightOnly(float);                      // 对应 SetWeight（保留 fade）
    float targetWeight() const; void setTargetWeight(float);
    float fadeSpeed() const; void setFadeSpeed(float);
    float effectiveWeight() const;                  // 累乘父权重
    void applyWeight();                             // 应用到父输入口

    // 淡入淡出
    void startFade(float targetWeight);
    void startFade(float targetWeight, float duration);
    virtual void stop();                            // Weight=0

    // 速度
    float speed() const; void setSpeed(float);
    float effectiveSpeed() const;

    // IK 标志
    bool applyAnimatorIK() const; void setApplyAnimatorIK(bool);
    bool applyFootIK() const; void setApplyFootIK(bool);

    // 更新（由 pre-update 驱动）
    virtual void update(bool& needsMoreUpdates);
};
```

### 2.1 权重与淡入淡出（务必逐字对齐）

原项目 `AnimancerNode.cs`：
- `weight` 的 setter：`SetWeight(value); TargetWeight=value; FadeSpeed=0;`（`:514-523`）。
- `setWeightOnly`：仅改 `_Weight` 并标脏（`:534-546`）。
- `effectiveWeight`：自身权重 × 所有父权重（`:596-611`）。
- `startFade(target,duration)`（`:657-689`）：
  ```
  TargetWeight = target
  if target == Weight:
      target==0 ? Stop() : (FadeSpeed=0; OnStartFade()); return
  if duration <= 0: FadeSpeed = +inf
  else: FadeSpeed = |Weight - target| / duration
  OnStartFade(); RequireUpdate()
  ```
- `update`（`:341-345`）：`updateFade(); applyWeight();`
- `updateFade`（`:713-762`）：
  ```
  if FadeSpeed==0 { needsMoreUpdates=false; return; }
  dirtyWeight = true
  step = |FadeSpeed * parentEffectiveSpeed * deltaTime|
  delta = target - current
  if |delta| > step: current += sign(delta)*step; needsMoreUpdates=true; return
  current = target; needsMoreUpdates=false
  target==0 ? Stop() : FadeSpeed=0
  ```
- `applyWeight`（`:560-588`）：若父 `KeepChildrenConnected==false`，权重为 0 时断开、非 0 时按需连接；然后 `setInputWeight(index, weight)`。
- `effectiveSpeed`：`Speed * 父链 Speed`（`:854-858`）。

### 2.2 连接策略

- `ConnectToGraph`：`graph.connect(playable,0,parent.playable,index); parent.playable.setInputWeight(index,weight)`（`:250-271`）。
- `ApplyConnectedState`：权重≠0 或父 keepChildren 时连接，否则只 `RequirePreUpdate`（`:287-309`）。
- `ConnectAllChildrenToGraph` / `DisconnectWeightlessChildrenFromGraph`：批量操作（`:445-465`），供 root `KeepChildrenConnected` 切换用。

## 3. AnimancerState（状态）

原项目：`Internal/Core/AnimancerState.cs`。

```cpp
class AnimancerState : public AnimancerNode {
public:
    // 图/Root
    void setRoot(AnimancerPlayable*);
    AnimancerNode* parent() const override;

    // Key / Clip
    Key key() const; void setKey(Key);
    virtual const Clip* clip() const { return nullptr; }   // ClipState 覆盖

    // 播放
    bool isPlaying() const; void setIsPlaying(bool);
    bool isActive() const;  // isPlaying && TargetWeight>0
    bool isStopped() const; // !isPlaying && Weight==0
    void play();            // IsPlaying=true; Weight=1; clear events
    void stop() override;   // Weight=0; IsPlaying=false; Time=0; clear events

    // 时间
    float time() const;  void setTime(float);
    double timeD() const; void setTimeD(double);   // 延迟设置 _MustSetTime
    virtual double rawTime() const; void setRawTime(double);
    float normalizedTime() const; void setNormalizedTime(float);
    double normalizedTimeD() const;
    void moveTime(double time, bool normalized);   // 会触发 root motion/events

    // 时长
    virtual float length() const = 0;
    virtual bool isLooping() const { return false; }
    float normalizedEndTime() const; void setNormalizedEndTime(float);
    float duration() const; void setDuration(float);
    float remainingDuration() const; void setRemainingDuration(float);

    // 事件
    EventSequence* events(); void setEvents(EventSequence*);
    bool hasEvents() const;

    // 更新（在 pre-update）
    void update(bool& needsMoreUpdates) override;   // fade/applyWeight + IsPlaying 脏标记 + _MustSetTime
    void destroy();
    virtual AnimancerState* clone(AnimancerPlayable* root) = 0;
};
```

关键点（逐条照抄）：
- **时间延迟设置**：在 post-update 中改时间会置 `_MustSetTime=true` 并 `RequirePreUpdate`，下一帧 early-update 才真正写入 playable（`AnimancerState.cs:472-526`）。原因见 [01 §3](01-总体架构.md)。
- **两帧 SetTime**：`setRawTime` 连续 `setTime(t)` 两次，避免误触发中间事件（`AnimancerState.cs:540-554`）。
- **IsPlaying 脏标记**：初始为 true（Playables 默认播放），首次 update 时按需 Pause（`AnimancerState.cs:262,315-328,803-822`）。
- **Stop 清事件**：`stop()` 会清空事件（除非 `AutomaticallyClearEvents==false`，`AnimancerState.cs:375-383`）。
- `clip()`：`ClipState` 返回其 Clip；`Mixer` 返回 null。

## 4. AnimancerLayer（层）

原项目：`Internal/Core/AnimancerLayer.cs`。

```cpp
class AnimancerLayer : public AnimancerNode {
public:
    AnimancerState* currentState() const;
    int commandCount() const;                 // CurrentState 变化次数
    bool isAdditive() const; void setIsAdditive(bool);
    void setMask(const AvatarMask*);

    int childCount() const override;
    AnimancerState* getChild(int) const override;
    void addChild(AnimancerState*);           // 分配新输入口

    // 创建/获取
    ClipState* createState(const Clip&);
    AnimancerState* getState(Key& key);       // 处理 key 链
    AnimancerState* getOrCreateState(const Clip&, bool allowSetClip=false);
    AnimancerState* getOrCreateState(ITransition&);
    AnimancerState* getOrCreateWeightlessState(AnimancerState*);

    // 播放
    AnimancerState* play(const Clip&);
    AnimancerState* play(AnimancerState*);
    AnimancerState* play(AnimancerState*, float fade, FadeMode);
    AnimancerState* play(ITransition&);
    AnimancerState* tryPlay(Key);
    void stop() override;

    bool isPlayingClip(const Clip&); bool isAnyStatePlaying();
    float getTotalWeight();

    static float weightlessThreshold;   // 默认 0.1
    static int   maxCloneCount;         // 默认 3
};
```

### 4.1 播放/交叉淡入（`AnimancerLayer.cs:656-906`）

- `play(state)`（立即）：若层权重为 0 且目标 0 → 置 1；`currentState=state`（`commandCount++`）；`state->play()`；其它 state `stop()`。
- `play(state, fade, mode)`：
  - `fade<=0` 或（`Root.SkipFirstFade && index==0 && Weight==0`）→ 直接 `play`；`FromStart/NormalizedFromStart` 时 `state.TimeD=0`。
  - 否则 `evaluateFadeMode(mode,...)` → `startFade(1, layerFadeDuration)`；层权重为 0 时直接 `play(state)`。
  - 否则：`currentState=state`；若目标已在淡入且更快完成则 `OnStartFade()`，否则 `state.isPlaying=true; state.startFade(1,fade)`，其它 state `startFade(0,fade)`。

### 4.2 `evaluateFadeMode`（`AnimancerLayer.cs:859-906`）

```
layerFadeDuration = fadeDuration
FixedSpeed:            fade *= |1 - state.Weight|; layerFade *= |1 - Weight|
FixedDuration:         (不变)
FromStart:             state = getOrCreateWeightlessState(state)
NormalizedSpeed:       fade *= |1-state.Weight| * state.Length; layerFade *= |1-Weight| * Length
NormalizedDuration:    fade *= Length; layerFade *= Length
NormalizedFromStart:   state = getOrCreateWeightlessState(state); fade *= Length; layerFade *= Length
```

`FadeMode` 定义见 `Internal/Core/FadeMode.cs`（7 种）。

### 4.3 权重无关状态（weightless 克隆，`AnimancerLayer.cs:491-616`）

用途：让同一动画“淡入自身”（如连续攻击）。算法：
1. 若 state 无父：`Weight=0`，返回它。
2. 若已在正确层且 `Weight<=threshold`：直接返回。
3. 沿 key 链找已存在的低权重复本；找不到则 `clone`，`Weight=0`，以“上一个状态”为 key 注册，加入层；超过 `maxCloneCount` 时退化为“最低权重”的那个。
4. 返回前 `TimeD=0`。

## 5. LayerList

原项目：`AnimancerPlayable.LayerList.cs`。

- 持有 `AnimationLayerMixerPlayable`（本库 `LayerMixerNode`）与 `AnimancerLayer[]`。
- `count` 增减：增加→`Add()`（`layerMixer.setInputCount(count)` + new Layer）；减少→销毁子图 + `destroyStates()`。
- `setMask(index, mask)` → `layerMixer.setLayerMask...`；`setAdditive`。
- 默认容量 4（`DefaultCapacity`），超出翻倍。
- 平均速度 `averageVelocity` = Σ(layer.velocity * layer.Weight)。

## 6. StateDictionary

原项目：`AnimancerPlayable.StateDictionary.cs`。

- `registerState(state)` / `unregister(state)`：以 `state.key()` 为键。
- `getOrCreate(clip)` / `getOrCreate(transition)`：已存在则复用；同 key 不同 clip 时按 `allowSetClip` 处理。
- `tryGet(key,out state)`、`get(key)`、`current()`（Layer 0 的 `currentState()`）、`destroy(clip/state)`。
- `createIfNew(std::vector<const Clip*>)` / `createIfNew(initializer_list)`：对每个非空 Clip 调用 `getOrCreate`。
- 冲突错误文案对齐原 `GetClipMismatchError`。
- `gatherAnimationClips(clips)`：递归遍历已注册状态；`gatherClip` 负责去重。

## 7. Transition 体系

原项目：`Utilities/Transitions/*` + `Internal/Interfaces/ITransition.cs`。

```cpp
class ITransition {
public:
    virtual ~ITransition() = default;
    virtual AnimancerState* createState() = 0;
    virtual void apply(AnimancerState&) = 0;
    virtual float fadeDuration() const = 0;
    virtual FadeMode fadeMode() const = 0;
    virtual Key key() const = 0;      // 默认 this
    virtual bool isValid() const { return true; }
};

template <class TState>
class Transition : public ITransition {
    float _fadeDuration = 0.25f;
    EventSequence* _events = nullptr;      // 拥有/引用
    TState* _state = nullptr;              // 最近一次 apply 的 state
public:
    void apply(AnimancerState& state) override {
        state.setEvents(_events);
        _state = static_cast<TState*>(&state);
    }
};
```

具体类型（与原文一致）：
- `ClipTransition : Transition<ClipState>`：`clip`、`speed`、`normalizedStartTime`；`fadeMode = isnan(start)? FixedSpeed : FromStart`；`createState -> new ClipState(clip)`；`apply -> applyDetails(speed,start) + base.apply`（`ClipTransitionAsset.cs:28-160`）。
- `ClipTransitionSequence`：多个 Clip 串联，用 End 事件接下一段；已实现 `others`、`lastTransition`、序列级 `length/maximumDuration`、`endEvent`、`addEvent`、`copyFrom` 和首段 End 事件搬迁（`ClipTransitionSequence.cs`）。
- `ManualMixerTransition<TMixer>` / `MixerTransition<TMixer,TParameter>`：`animations[]`、`speeds[]`、`thresholds[]`、`defaultParameter`；`initializeState` 设阈值与参数（`MixerTransition.cs:52-58`）。
- `LinearMixerTransition` / `MixerTransition2D` / `ManualMixerTransition`：对应各混合器。

### 7.1 Transition Asset（共享 vs 非共享）

原项目：`AnimancerTransitionAssetBase.cs` + `.UnShared.cs`。
- `TransitionAsset` = 可复用的配置资源（对应 ScriptableObject）。`createState` 委托内部 transition。
- `UnShared<TAsset>`：包装 asset，但**各自持有 BaseState 与 Events 副本**，避免多对象共享同一 transition 导致事件串扰（`UnShared.cs:23-216`）。本库实现 `UnShared<T>` 模板，行为一致。

## 8. Fade 与 Easing

- 内置线性淡入淡出见 §2.1。
- `FadeMode` 7 种见 §4.2。
- `CustomFade`（`Utilities/Custom Fade/CustomFade.cs`）：用自定义曲线替换线性插值。
  - 记录目标节点 / 其它淡出节点 / 层 / `CommandCount`；每帧按 `progress` 计算权重。
  - 若目标被销毁、层变化或 `CommandCount` 变化 → 取消（`CustomFade.cs:142-153`）。
- `Easing`（`Utilities/Custom Fade/Easing.cs`）：easings.net 全套 In/Out/InOut + 导数 + ranged 变体。本库移植为自由函数/函数对象。

## 9. 事件

原项目：`Internal/Core/AnimancerEvent.cs`、`AnimancerEvent.Sequence.cs`、`AnimancerState.EventDispatcher.cs`。

### 9.1 事件与序列

```cpp
struct AnimancerEvent { float normalizedTime; Action callback; };

class EventSequence {
public:
    int count() const;                       // 含末尾 End 事件
    AnimancerEvent& operator[](int);
    AnimancerEvent& operator[](std::string_view name);
    AnimancerEvent endEvent();               // 最后一个（OnEnd）
    float normalizedEndTime() const; void setNormalizedEndTime(float);

    int add(AnimancerEvent);                 // 按时间有序插入（End 之前）
    int add(float normalizedTime, Action);
    void addCallback(int index, Action);
    void remove(int index);
    void clear();
    // 名字
    void setName(int, std::string_view); int indexOf(std::string_view);
};
float getDefaultNormalizedStartTime(float speed); // speed<0 ? 1 : 0
float getDefaultNormalizedEndTime(float speed);   // speed<0 ? 0 : 1
```

- 序列按 `normalizedTime` 升序；`OnEnd` 是末尾事件，`NormalizedEndTime` 可覆盖（默认随速度方向取 1 或 0）。原 `AnimancerEvent.Sequence.cs:216,252,274,282`。
- 事件在 `Play/Stop/OnStartFade` 时自动清空（除非 `AutomaticallyClearEvents==false`）。

### 9.2 触发（EventDispatcher）

原项目：`AnimancerState.EventDispatcher.cs`。要点：
- 它是一个 `IUpdatable`，注册在 **post-update** 列表（每帧最后执行），因此可以在事件里安全地切换动画（时间改动会延迟到下一帧）。
- 维护 `_PreviousTime`、`_NextEventIndex`、`_SequenceVersion`、`_IsLooping`、`_WasPlayingForwards`。
- 前向：`while index<count && currentTime > eventTime*dir: invoke; next`（非循环）。循环：用 `GetLoopDelta` 处理跨圈，每圈都触发（`EventDispatcher.cs:511-565`）。
- 后向：先把时间取负统一处理，索引从末尾开始（`ValidateNextEventIndex`，`:569-619`）。
- 零长度：每帧触发 End（`UpdateZeroLength`，`:464-507`）。
- End 事件在“越过结束点后每帧”触发（因此有 `EndEventInterrupt` 警告机制）。

## 10. 组件（MonoBehaviour）等价物

原项目：`AnimancerComponent.cs` / `SoloAnimation.cs` / `NamedAnimancerComponent.cs` / `HybridAnimancerComponent.cs`。C++ 引擎无 MonoBehaviour，因此提供：

```cpp
class AnimancerAnimator {           // 相当于 AnimancerComponent 的核心职责
public:
    Skeleton* skeleton;
    AnimancerPlayable* playable;    // 拥有
    void initialize(Skeleton&);     // create graph + createOutput
    void destroy();                 // destroyGraph
    bool isInitialized() const;
    void update(float dt);          // playable->evaluate(dt)
    void evaluate(float dt);        // update 别名
    void evaluate();
    float speed() const;
    void setSpeed(float);
    bool isGraphPlaying() const;
    void pauseGraph();
    void unpauseGraph();
    const std::vector<Mat4>& palette() const;   // 计算蒙皮调色板

    AnimancerState* play(const Clip&);
    AnimancerState* play(const Clip&, float fadeDuration, FadeMode);
    AnimancerState* play(ITransition&);
    AnimancerState* play(ITransition&, float fadeDuration, FadeMode);
    AnimancerState* play(AnimancerState*);
    AnimancerState* play(AnimancerState*, float fadeDuration, FadeMode);
    AnimancerState* tryPlay(Key);
    AnimancerState* tryPlay(Key, float fadeDuration, FadeMode);
    void stop();
    AnimancerState* stop(const Clip&);
    AnimancerState* stop(Key);
    bool isPlaying() const;
    bool isPlaying(const Clip&) const;
    bool isPlaying(Key) const;
    bool isPlayingClip(const Clip&) const;
    AnimancerLayer* layer(int index);
};

class SoloAnimation : public IAnimationClipCollection {
public:
    void initialize(Skeleton&, const HumanoidAvatar* = nullptr);
    bool play(const Clip&);
    void stop();
    void update(float dt);
    void setIsPlaying(bool);
    void setSpeed(float);
    void setTime(float);
    void setNormalizedTime(float);
    const Pose& pose() const;
    const std::vector<Mat4>& palette() const;
};

class NamedAnimancerAnimator : public IAnimationClipCollection {
public:
    void initialize(Skeleton&, const HumanoidAvatar* = nullptr, int layers = 1);
    void setAnimations(std::initializer_list<NamedAnimation>);
    bool addAnimation(std::string name, const Clip&);
    const Clip* findAnimation(std::string_view name) const;
    const Clip* defaultAnimation() const;
    bool setDefaultAnimation(std::string_view name);
    AnimancerState* playDefault();
    AnimancerState* start();
    AnimancerState* play(std::string_view name);
    AnimancerState* play(std::string_view name, float fadeDuration, FadeMode);
    AnimancerState* stop(std::string_view name);
    bool isPlaying(std::string_view name) const;
};
```
- `play(clip)` / `play(clip, fade)` / `play(state)` / `play(state, fade)` 按原
  `AnimancerPlayable.GetLocalLayer` 保留已注册状态所属 Layer；`tryPlay(key)` 同样保留
  Layer，`stop(key/clip)` 返回并重绕对应 state。
- `AnimancerLayer::getState(key)` 按原 `GetState(ref object)` 沿跨 Layer 状态 key 链查找；
  `getOrCreateState(clip/transition)` 在目标 Layer 无匹配时以链尾状态为 key 创建独立状态。
- 目标 Layer 播放另一 Layer 的状态时，按原 `AnimancerLayer.GetOrCreateState(AnimancerState)`
  克隆状态（含时间/速度/权重/事件），不移动原状态。
- `NamedAnimancerAnimator`（对应 `NamedAnimancerComponent.cs`）：显式注册“名字→Clip”，
  支持默认动画、按名字播放/淡入/停止；`start()` 按 `playAutomatically` 模拟原 `OnEnable`。
- `SoloAnimation`（对应 `Internal/Core/SoloAnimation.cs`）：单 Clip 直接采样门面，
  不创建 Layer/Fade/State，提供 `play/stop/update`、播放暂停、速度、时间/归一化时间、
  Pose/Palette；Humanoid Clip 必须提供 `HumanoidAvatar`，不自动接线事件或根运动。
- `HybridAnimancerComponent`（与 AnimatorController 共存）：本库无对应概念，跳过；其“在输出后插入 job”的能力由 `insertOutputPlayable/insertOutputJob` 提供（原 `AnimancerPlayable.cs:513-535`）。

## 11. 文件清单

```
include/anim/animancer/AnimancerPlayable.h
include/anim/animancer/AnimancerNode.h
include/anim/animancer/AnimancerState.h
include/anim/animancer/AnimancerLayer.h
include/anim/animancer/LayerList.h
include/anim/animancer/StateDictionary.h
include/anim/animancer/AnimationClipCollection.h
include/anim/animancer/ClipState.h
include/anim/animancer/FadeMode.h
include/anim/animancer/Transition.h              // ClipTransition + RetargetClipTransition + TransitionAsset/UnShared
include/anim/animancer/ClipTransitionSequence.h
include/anim/animancer/MixerTransitions.h        // Manual/Linear/2D
include/anim/animancer/Event.h
include/anim/animancer/EventSequence.h
include/anim/animancer/EventDispatcher.h
include/anim/animancer/CustomFade.h
include/anim/animancer/Easing.h
include/anim/animancer/AnimancerAnimator.h
include/anim/animancer/SoloAnimation.h
include/anim/animancer/NamedAnimancerAnimator.h
include/anim/animancer/RetargetClipState.h            // P9 可选跨骨架状态
include/anim/animancer/TimeSynchronizer.h
include/anim/animancer/TimeSynchronizationGroup.h
include/anim/animancer/DirectionalAnimationSet.h
include/anim/animancer/DirectionalAnimationSet8.h
include/anim/animancer/DirectionalClipTransition.h
include/anim/animancer/MixerParameterTween.h
include/anim/animancer/ExitEvent.h
include/anim/animancer/DontAllowFade.h
include/anim/animancer/fsm/StateMachine.h  // 含 StateSelector
```

## 12. 与原项目对照校验点

1. `startFade` 的常量与分支必须逐字对齐 §2.1。
2. `play(fade)` 的“已在淡入且更快则继续”判定：`state.IsPlaying && TargetWeight==1 && (Weight==1 || FadeSpeed*fade > |1-Weight|)`（`AnimancerLayer.cs:764-780`）。
3. `Fractional`/归一化时间、`Duration`、`RemainingDuration` 公式（`AnimancerState.cs:709-777`）。
4. 事件正/反向与循环触发（§9.2）。
5. `weightless` 克隆上限与退化策略（§4.3）。

## 13. P3 实现备注（已完成部分）

已实现并通过测试（`test_events / test_fade / test_layer / test_animancer`）：

- **图结构**：
  - `LayerMixerNode` 为输出根；每个 `AnimancerLayer` 拥有一个 `MixerNode`；
    `AnimancerState`（P3 只有 `ClipState`）拥有一个 `ClipNode`。
  - 新增 `AnimancerRootNode`（`AnimancerNode` 子类）承载**根 Speed**，让
    `EffectiveSpeed/EffectiveWeight` 能沿 `state → layer → root` 连乘。
- **时间**：`AnimancerState` 是唯一权威。`ClipNode::setAutoAdvanceTime(false)`，
  每帧 pre-update 由 State 推进时间并写入图节点；避免图节点与状态双重推进。
- **更新列表**：与 `AnimancerPlayable.UpdateAll` 一致，每帧**单次**遍历；
  `needsMoreUpdates=false` 的节点移出列表。新增
  `IUpdatable::requiresContinuousUpdate()`：正在播放的 State 返回 true，
  从而每帧留在 pre-update 中（Unity 由 Playables 引擎推进时间，本库需自己保留）。
- **事件**：`EventDispatcher` 是 post-update；本库在 `graph.evaluate()` **之后**
  统一运行 post 列表（等价于原 PostUpdate 节点），事件里改时间会延迟到下一帧。
- **淡入淡出**：`StartFade / UpdateFade / ApplyWeight / EffectiveWeight` 逐行对齐
  （`AnimancerNode.cs:514-762`）。
- **层**：立即 Play / 交叉淡入 / 6 种 `FadeMode` / weightless 克隆
  （克隆以“源 State 指针”为 key 注册）均已实现。
- **过渡**：`ITransition` + `ClipTransition` + `TransitionAsset<T>` / `UnShared<T>`。
  `ClipTransition::apply` 设置 speed / normalizedStartTime / events。
- **组件等价物**：`AnimancerAnimator`（initialize / update / palette / play / fade / stop / isPlaying / layer）；
  `SoloAnimation`（单 Clip 直接采样、播放控制、Pose/Palette）。

### 13.1 P3-b（已完成）

- `ManualMixerState` / `LinearMixerState` / `DirectionalMixerState` / `CartesianMixerState`
  及其权重算法（详见 docs/08 §11）。
- `ManualMixerTransition` / `LinearMixerTransition` / `MixerTransition2D`。
- `CustomFade`（曲线式自定义淡入淡出）；`Easing` 已有。
- `ClipTransitionSequence`（End 事件串联多段）；见 `include/anim/animancer/ClipTransitionSequence.h`。

### 13.2 P1 便利功能（已完成）

- `TimeSynchronizer<T>`：`storeTime` 记录状态/层的归一化时间，`syncTime` 仅在 group
  匹配且非默认 group 时设置 `TimeD = NormalizedTime * Length + DeltaTime * EffectiveSpeed`。
- `TimeSynchronizationGroup`：以 `const void*` key 管理分组，支持 `AnimancerPlayable` /
  显式 `AnimancerState` 两种用法；`setAnimancer` 会清空已存时间。
- `DirectionalAnimationSet` / `DirectionalAnimationSet8`：4/8 方向 clip、方向向量、
  `snap` 和整组 clip 收集；`DirectionalClipTransition` 从集合选择 clip 后按普通
  `ClipTransition` 播放，并收集整个集合。
- `MixerParameterTweenFloat` / `MixerParameterTweenVector2`：在 pre-update 中按
  `StartValue→EndValue` 线性插值 `MixerState<T>::Parameter`，到达后自动取消。
- `ExitEvent`：在 post-update 检查 `EffectiveWeight`，归零时回调并从更新列表移除；
  节点销毁会取消其挂起事件。
- `DontAllowFade`：pre-update 检查所有层和当前状态，发现 `FadeSpeed != 0` 时取消淡入淡出；
  `assertEnabled` 仅 debug 启用，`enable` 始终启用。
- `StateMachine<TState>::StateSelector` + `IPrioritizable`：按降序优先级尝试状态，
  重复优先级会断言；可直接传给 `canSetState/trySetState/tryResetState`。

