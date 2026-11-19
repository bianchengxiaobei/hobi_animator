# 15 · API 参考（头文件契约）

本文件是接口参考。**实际签名以头文件及本文“实际签名”小节为准**；历史设计稿已移除，不构成实现契约。签名可在不破坏语义的前提下微调（例如用 `std::span`/`std::string_view`）。

命名空间：`anim`（FSM 在 `anim::fsm`）。类型用 `PascalCase`，方法用 `camelCase`（与文档一致）。

---

## core

```cpp
// core/Math.h
using Vec2=glm::vec2; using Vec3=glm::vec3; using Vec4=glm::vec4;
using Quat=glm::quat; using Mat3=glm::mat3; using Mat4=glm::mat4;

// core/Transform.h
// alignas(16)、48 字节：与 mecanim math::xform 的 t/q/s 三个 float4 对齐
struct alignas(16) Transform { Vec3 translation{0}; Quat rotation{1,0,0,0}; Vec3 scale{1}; };

// core/Hash.h
using StringId = uint64_t;
constexpr StringId hashString(std::string_view);

// core/Pool.h
template<class T> class Pool {
public: T* acquire(); void release(T*); void reserve(size_t);
private: std::vector<std::unique_ptr<T>> _all; std::vector<T*> _free; };

// core/Serialization.h
class Reader { public: explicit Reader(std::span<const uint8_t>); uint32_t version() const;
  template<class T> T read(); std::string_view readString(); bool eof() const; };
class Writer { public: void writeU32(uint32_t); template<class T> void write(const T&);
  void writeString(std::string_view); std::vector<uint8_t> finish(); };
```

## clip

```cpp
// clip/Curve.h
struct Keyframe { float time,value,inSlope,outSlope,inWeight,outWeight; uint8_t tangentMode,weightedMode; };
class Curve { public: std::vector<Keyframe> keys; float length() const;
  float evaluate(float t) const; float evaluateUnclamped(float t) const; };

// clip/Clip.h
enum class ClipType : uint8_t { BoneLocal=0, HumanoidMuscle=1 };
struct EventMarker { float normalizedTime; StringId name; };
struct PropertyChannel { StringId path; StringId attribute; Curve curve; };
struct Clip { ClipType type; float length; bool looping; float averageSpeed;
  std::vector<EventMarker> markers; std::vector<PropertyChannel> properties;
  const PropertyChannel* findProperty(std::string_view path, std::string_view attribute) const;
  virtual ~Clip()=default; };

struct BoneChannel { int boneIndex; Curve position[3], rotation[4], scale[3];
  uint8_t hasPosition:1, hasRotation:1, hasScale:1; };
struct BoneLocalClip : Clip { std::vector<BoneChannel> channels; };

struct HumanoidClip : Clip {
  std::vector<Curve> muscles; Curve rootT[3], rootQ[4];
  Curve leftFootT[3],rightFootT[3],leftFootQ[4],rightFootQ[4];
  Curve leftHandT[3],rightHandT[3],leftHandQ[4],rightHandQ[4];
  bool hasFootQ=false, hasHandQ=false;
};

// clip/Sample.h
void sampleClip(const Clip&, float t, const Skeleton&, const HumanoidAvatar*, Pose& out);
```

## skeleton

```cpp
// skeleton/Skeleton.h
struct Bone { StringId name; int32_t parent=-1; Transform restLocal; Mat4 inverseBind{1.0f}; };
class Skeleton {
public:
  std::vector<Bone> bones;
  int addBone(StringId, int32_t parent, const Transform&, const Mat4& inverseBind = Mat4(1.0f));
  int find(StringId) const noexcept;
  int boneCount() const noexcept; int rootIndex() const noexcept;
  const Transform& restLocal(int) const; const Bone& bone(int) const;
  void clear() noexcept; void reserve(std::size_t); bool isTopological() const noexcept; };

// skeleton/Pose.h
struct Pose {
  std::vector<Transform> local;
  std::size_t boneCount() const noexcept; bool matches(const Skeleton&) const noexcept;
  void resizeLike(const Skeleton&); void resizeTo(std::size_t);
  void resetToRest(const Skeleton&); };

// skeleton/Blend.h
float totalWeight(Span<const float>);
void normalizeWeights(Span<float>);                    // 对齐 ManualMixerState.NormalizeWeights
void lerpPose(const Pose& a, const Pose& b, float t, Pose& dst);
void resetAccumulator(const Skeleton&, Pose&);
void accumulatePose(Pose& acc, const Pose& src, float weight, const Pose* reference=nullptr);
void normalizePose(Pose&);
void blendPoses(Span<const Pose*>, Span<const float>, Pose& out);

// skeleton/Palette.h
Mat4 computeBoneWorldMatrix(const Skeleton&, const Pose&, int, std::vector<Mat4>* scratch=nullptr);
void computeWorldMatrices(const Skeleton&, const Pose&, std::vector<Mat4>& out);
void computePalette(const Skeleton&, const Pose&, std::vector<Mat4>& out);

// skeleton/RootMotion.h
struct RootMotionDelta { Vec3 deltaPosition; Quat deltaRotation; bool hasMotion=false; };
RootMotionDelta extractRootMotion(const Pose& prev, const Pose& curr,
                                  const Skeleton&, int rootIndex=-1);

// clip/SampleClip.cpp
void sampleClip(const Clip&, float t, const Skeleton&, const HumanoidAvatar*, Pose& out);
// BoneLocalClip / HumanoidClip 均已实现（P1 / P4）。
```

## avatar

```cpp
// avatar/HumanBone.h（数值与 UnityEngine.HumanBodyBones 一致）
enum class HumanBone : uint8_t {
  Hips=0, LeftUpperLeg=1, RightUpperLeg=2, LeftLowerLeg=3, RightLowerLeg=4,
  LeftFoot=5, RightFoot=6, Spine=7, Chest=8, Neck=9, Head=10,
  LeftShoulder=11, RightShoulder=12, LeftUpperArm=13, RightUpperArm=14,
  LeftLowerArm=15, RightLowerArm=16, LeftHand=17, RightHand=18,
  LeftToes=19, RightToes=20, LeftEye=21, RightEye=22, Jaw=23,
  /* fingers... */ UpperChest=54, Count=55
};
const char* humanBoneName(HumanBone);
HumanBone humanBoneFromName(std::string_view);

// avatar/MuscleTable.h
inline constexpr int kMuscleCount = 95;
struct MuscleLimit { float minDegrees=-90, maxDegrees=90; };
struct MuscleEntry { const char* name; HumanBone bone; MuscleAxis axis;
                     MuscleKind kind; MuscleLimit defaultLimit; };
const MuscleEntry& muscleEntry(int id);
int findMuscleId(std::string_view name);

// avatar/HumanoidAvatar.h
struct HumanoidAxisBinding { int muscleId=-1; float sign=1; }; // Unity UVW
struct HumanoidAvatar {
  std::array<int32_t,kHumanBoneCount> boneMap;
  std::array<Transform,kHumanBoneCount> restLocal;
  std::array<Quat,kHumanBoneCount> preRot, postRot;
  std::array<MuscleLimit,kMuscleCount> limits;
  std::array<std::array<HumanoidAxisBinding,3>,kHumanBoneCount> uvw;
  std::array<bool,kHumanBoneCount> hasUvw;
  float hipsHeight = 0;
  void setAxis(HumanBone, int slot, int muscleId, float sign);
};
HumanoidAvatar createHumanoidAvatar(const Skeleton&, const std::array<int32_t,kHumanBoneCount>&,
                                    float hipsHeight=0);

// avatar/MuscleSpace.h
enum class HumanoidRootMode { Hips, MassCenter };
struct MuscleValues { std::array<float,kMuscleCount> values{}; };
void muscleSpaceToLocal(const MuscleValues&, const HumanoidAvatar&, const Skeleton&, Pose&);
HumanoidSample sampleHumanoidClip(const HumanoidClip&, float t);
void humanoidToLocal(const HumanoidSample&, const HumanoidAvatar&, const Skeleton&, Pose&,
                     float humanScale=1);

// avatar/HumanoidCalibration.h（P4.1）
struct MuscleCalibrationSample { int muscleId; float value; Quat localRotation; };
struct HumanoidCalibrationData { std::array<bool,kHumanBoneCount> hasRest;
  std::array<Quat,kHumanBoneCount> restRotation; std::vector<MuscleCalibrationSample> samples; };
struct HumanoidCalibrationReport { std::array<Quat,kHumanBoneCount> preRot, postRot;
  std::array<MuscleLimit,kMuscleCount> limits; std::array<bool,kHumanBoneCount> calibratedBones;
  std::array<bool,kMuscleCount> calibratedMuscles; float maxErrorDegrees; };
bool calibrateHumanoid(const HumanoidCalibrationData&, HumanoidCalibrationReport&,
                       std::string& error, float toleranceDegrees=0.1f);
void applyHumanoidCalibration(HumanoidAvatar&, const HumanoidCalibrationReport&);
```

## graph

```cpp
// core/Handle.h（模式参考 hdrp-bgfx/src/core/handle.h）
template <class Tag> struct Handle { uint32_t index=kInvalid, version=0;
  bool valid() const; explicit operator bool() const; };

// graph/PlayableHandle.h
struct PlayableNodeTag {};
using PlayableHandle = Handle<PlayableNodeTag>;
enum class PlayableNodeType : uint8_t { Script, Mixer, LayerMixer, Clip, RetargetClip, ScriptPose };

// graph/PlayableNode.h
class PlayableNode { public: virtual ~PlayableNode()=default;
  PlayableHandle handle() const; PlayableGraph* graph() const; uint32_t index() const;
  virtual PlayableNodeType type() const noexcept; virtual const char* typeName() const noexcept;
  virtual void onPlayableCreate(PlayableGraph&, PlayableHandle) {}
  virtual void onPlayableDestroy() {}
  virtual void prepareFrame(float);   // 默认推时间
  virtual void processFrame(float) {}
  virtual bool hasPose() const noexcept; virtual const Pose& outputPose() const;
  int inputCount() const; PlayableHandle input(int) const;
  PlayableNode* inputNode(int); const PlayableNode* inputNode(int) const;
  float inputWeight(int) const;
  float weight() const; void setWeight(float);
  float speed() const; void setSpeed(float);
  double time() const; void setTime(double); void setDuration(double);
  bool isLooping() const; void setLooping(bool);
  bool isPlaying() const; void play(); void pause(); bool isActive() const;
  protected: Pose _output; };

// graph/PlayableGraph.h
class PlayableGraph { public:
  template<class T,class...A> PlayableHandle create(A&&...);
  void destroy(PlayableHandle); void destroySubgraph(PlayableHandle);
  void connect(PlayableHandle src,int srcPort,PlayableHandle dst,int dstPort);
  void disconnect(PlayableHandle dst,int port);
  void setInputWeight(PlayableHandle,int,float); float getInputWeight(PlayableHandle,int) const;
  void setInputCount(PlayableHandle,int); int getInputCount(PlayableHandle) const;
  void setOutput(PlayableHandle); PlayableHandle output() const;
  PlayableNode* get(PlayableHandle) const noexcept; PlayableNode* root() const;
  void play(); void stop(); void evaluate(float dt);
  bool compile(); bool compiled() const; bool dirty() const;
  const std::string& lastError() const; const PlayableGraphDiagnostics& diagnostics() const;
  uint32_t frameId() const; int liveNodeCount() const;
  std::vector<PlayableHandle> nodeHandles() const; };

struct PlayableGraphDiagnostics { uint32_t nodeCount, reachableNodes, maxDepth; };

// graph/Nodes.h
class ClipNode final : public PlayableNode { public:
  ClipNode(const Clip&, const Skeleton&, const HumanoidAvatar* = nullptr); };
class RetargetClipNode final : public PlayableNode { public:
  RetargetClipNode(const Clip&, const Skeleton& source, const HumanoidAvatar& sourceAvatar,
                   const Skeleton& target, const HumanoidAvatar& targetAvatar); };
class MixerNode final : public PlayableNode {};
class LayerMixerNode final : public PlayableNode { public:
  void setLayerAdditive(int,bool); bool isLayerAdditive(int) const;
  void setLayerMask(int,const AvatarMask*); const AvatarMask* layerMask(int) const; };
class ScriptNode final : public PlayableNode { public:
  using Callback = std::function<void(PlayableNode&,float)>;
  void setPrepareCallback(Callback); void setProcessCallback(Callback);
  bool hasPose() const noexcept; const Pose& outputPose() const; };
class ScriptPoseNode final : public PlayableNode { public:
  explicit ScriptPoseNode(const Skeleton&);
  IAnimationJob* job = nullptr; IJobScheduler* scheduler = nullptr; };

// skeleton/AvatarMask.h
struct AvatarMask { std::vector<float> boneWeights;
  float weightFor(int bone) const noexcept; void resize(std::size_t, float = 1.0f); };
```
```

## animancer

> **权威性约定**：本节以“实际签名（当前头文件）”为准。
> 早期设计稿已移除；以下只列实际签名与头文件索引，不应引用历史版本。
> 完整实现说明见 `docs/07-Animancer门面.md §13`。

### 实际签名（当前头文件）

```cpp
// animancer/Transition.h（节选）
class ITransition : public IAnimationClipCollection { public: virtual ~ITransition()=default;
  virtual AnimancerState* createState(AnimancerPlayable& root)=0;
  virtual void apply(AnimancerState&)=0;
  virtual float fadeDuration() const=0; virtual FadeMode fadeMode() const=0;
  virtual Key key() const; virtual EventSequence* events(); virtual bool isValid() const;
  void gatherAnimationClips(std::vector<const Clip*>&) const override {} };

class ClipTransition : public ITransition { public:
  const Clip* clip=nullptr; float fadeDurationValue=0.25f; float speedValue=1.0f;
  float normalizedStartTime=std::numeric_limits<float>::quiet_NaN();
  EventSequence eventSequence;
  AnimancerState* createState(AnimancerPlayable&) override; void apply(AnimancerState&) override;
  float fadeDuration() const override; FadeMode fadeMode() const override;
  EventSequence* events() override; bool isValid() const override; Key key() const override;
  virtual float length() const; virtual float maximumDuration() const;
  virtual bool isLooping() const noexcept; virtual float averageAngularSpeed() const;
  virtual Vec3 averageVelocity() const; virtual void gatherClips(std::vector<const Clip*>&) const;
  void gatherAnimationClips(std::vector<const Clip*>&) const override;
  virtual void copyFrom(const ClipTransition&); };

// animancer/ClipTransitionSequence.h（已实现）
class ClipTransitionSequence : public ClipTransition { public:
  std::vector<ClipTransition> others;
  ClipTransition& lastTransition() noexcept;
  const ClipTransition& lastTransition() const noexcept;
  AnimancerEvent& endEvent() noexcept;
  const AnimancerEvent& endEvent() const noexcept;
  void apply(AnimancerState&) override;
  bool isValid() const override;
  bool isLooping() const noexcept override;
  float length() const override;
  float maximumDuration() const override;
  float averageAngularSpeed() const override;
  Vec3 averageVelocity() const override;
  void gatherClips(std::vector<const Clip*>&) const override;
  void copyFrom(const ClipTransitionSequence&);
  void addEvent(float time, bool normalized, std::function<void()> callback); };
```

> 实际签名要点：
> - `AnimancerPlayable` 使用 `layers()` / `states()` 方法与 `totalWeight()`；
>   `createOutput(Skeleton&)` 只绑定骨架，不再返回输出对象。
> - `AnimancerState` 没有 `rawTime`：State 时间是唯一权威，`ClipNode` 关闭了
>   自动推进（`setAutoAdvanceTime(false)`）。
> - `ClipState` 构造为 `ClipState(AnimancerPlayable*, const Clip&)`。
> - `ITransition::createState(AnimancerPlayable&)`。
> - `CustomFade` 与混合器 Transition 已实现（见 `include/anim/animancer/MixerStates.h` / `MixerTransitions.h`）。
> - `ClipTransitionSequence` 已实现，支持串联、序列级 length/events、`copyFrom`。

### 其他已实现 API

以下类型不再复制声明，完整签名与行为以 `include/anim/animancer/*.h` 及 [07 §13](07-Animancer门面.md) 为准：
- `AnimancerPlayable` / `AnimancerNode` / `AnimancerState` / `AnimancerLayer` / `LayerList` / `StateDictionary` / `ClipState`
- `ManualMixerState` / `LinearMixerState` / `DirectionalMixerState` / `CartesianMixerState`
- `ManualMixerTransition` / `LinearMixerTransition` / `MixerTransition2D`
- `EventSequence` / `EventDispatcher` / `CustomFade` / `Easing` / `AnimancerAnimator` / `SoloAnimation` / `NamedAnimancerAnimator`
- `AnimationClipCollection.h`：`IAnimationClipCollection::gatherAnimationClips` + `gatherClip` 去重工具
- `StateDictionary::current()`、`createIfNew(...)`、`gatherAnimationClips(...)`
- `AnimancerLayer::getState(Key&)` 沿跨层 key 链查找；`createIfNew(...)`、`gatherAnimationClips(...)`；`LayerList` / `AnimancerPlayable` / `AnimancerAnimator` / `SoloAnimation` 同样可收集
- `AnimancerState` / `ManualMixerState` / `ClipTransition` / `ClipTransitionSequence` / `ManualMixerTransition` 支持递归收集
- `TimeSynchronizer<T>` / `TimeSynchronizationGroup`：按 group/key 同步 NormalizedTime
- `DirectionalAnimationSet` / `DirectionalAnimationSet8` / `DirectionalClipTransition`：4/8 方向选择和 snap
- `MixerParameterTween<T>` / `MixerParameterTweenFloat` / `MixerParameterTweenVector2`：混合器参数平滑
- `ExitEvent`：EffectiveWeight 归零时的 post-update 回调
- `DontAllowFade`：检测并取消不应发生的 Fade（debug 断言启用）
- `StateMachine<TState>::StateSelector` / `IPrioritizable`：按优先级尝试进入状态
- `AnimancerPlayable` 新增 `requireOwnedPreUpdate`、更新列表查询（`isPreUpdating/isPostUpdating` 等）

## avatar（`namespace anim`，`include/anim/avatar/`）

```cpp
// avatar/HumanBone.h（数值与 UnityEngine.HumanBodyBones 一致）
enum class HumanBone : uint8_t { Hips=0, ..., UpperChest=54, Count=55 };
const char* humanBoneName(HumanBone) noexcept;

// avatar/MuscleTable.h
inline constexpr int kMuscleCount = 95;
enum class MuscleAxis : uint8_t { X, Y, Z };
enum class MuscleKind : uint8_t { Rotation, Twist, Stretch, Spread };
struct MuscleLimit { float minDegrees=-90, maxDegrees=90; };
struct MuscleEntry { const char* name; HumanBone bone; MuscleAxis axis;
                     MuscleKind kind; MuscleLimit defaultLimit; };
const MuscleEntry& muscleEntry(int id);
const MuscleEntry* muscleEntries() noexcept;
int findMuscleId(std::string_view name) noexcept;

// avatar/HumanoidAvatar.h
struct HumanoidAvatar {
  std::array<int32_t,kHumanBoneCount> boneMap;   // -1 = 缺失
  std::array<Transform,kHumanBoneCount> restLocal;
  std::array<Quat,kHumanBoneCount> preRot, postRot;
  std::array<MuscleLimit,kMuscleCount> limits;
  float hipsHeight;
  bool hasBone(HumanBone) const noexcept; int boneIndex(HumanBone) const noexcept;
  const Transform& rest(HumanBone) const; MuscleLimit limit(int) const;
  void setLimit(int muscleId, float minDeg, float maxDeg);
  void setBone(HumanBone, int skeletonBoneIndex); };
HumanoidAvatar createHumanoidAvatar(const Skeleton&,
    const std::array<int32_t,kHumanBoneCount>& boneMap, float hipsHeight=0);

// avatar/MuscleSpace.h
struct MuscleValues { std::array<float,kMuscleCount> values; float& operator[](int); };
struct HumanoidSample { MuscleValues muscles; HumanoidRootMode rootMode; Vec3 rootT; Quat rootQ;
  bool hasRootT, hasRootQ; /* left/right Foot/Hand T/Q + hasFootT/Q, hasHandT/Q */ };
HumanoidSample sampleHumanoidClip(const HumanoidClip&, float time);
void muscleSpaceToLocal(const MuscleValues&, const HumanoidAvatar&,
                        const Skeleton&, Pose& out);
void humanoidToLocal(const HumanoidSample&, const HumanoidAvatar&,
                     const Skeleton&, Pose& out, float humanScale=1.0f);

// avatar/HumanoidIK.h
struct HumanoidGoalWeights { float leftFootPosition, leftFootRotation,
  rightFootPosition, rightFootRotation, leftHandPosition, leftHandRotation,
  rightHandPosition, rightHandRotation; /* defaults 0 */
  float legStretch = 0.05f, armStretch = 0.05f; };
void applyHumanoidGoals(const HumanoidSample&, const HumanoidAvatar&,
                        const Skeleton&, Pose&, const HumanoidGoalWeights&,
                        float humanScale=1.0f);
struct HumanoidLookAtWeights { float clamp, body, head, eyes; };
void applyHumanoidLookAt(const HumanoidAvatar&, const Skeleton&, Pose&,
                         const Vec3& targetPosition,
                         const HumanoidLookAtWeights& = {});
void applyHumanoidFullBodySolve(const HumanoidSample&, const HumanoidAvatar&,
                                const Skeleton&, Pose&, const Vec3& lookAtTarget,
                                const HumanoidLookAtWeights&,
                                const HumanoidGoalWeights&,
                                float humanScale=1.0f);
```

## fsm（`namespace anim::fsm`）

```cpp
// IState.h
struct IState { virtual ~IState()=default;
  virtual bool canEnterState() const=0; virtual bool canExitState() const=0;
  virtual void onEnterState()=0; virtual void onExitState()=0; };
class State : public IState { /* 全 true / 空 */ };
template<class TState> struct IOwnedState { virtual StateMachine<TState>* ownerStateMachine()=0; };

// StateChange.h
template<class TState> class StateChange { static bool isActive();
  static StateMachine<TState>* stateMachine(); static TState* previousState(); static TState* nextState();
  StateChange(StateMachine<TState>*,TState*,TState*); ~StateChange(); /* RAII */ };

// StateMachine.h
template<class TState> class StateMachine { public:
  TState* currentState() const; TState* previousState() const; TState* nextState() const;
  void initializeAfterDeserialize();
  bool canSetState(TState*); TState* canSetState(std::span<TState*>);
  bool trySetState(TState*); bool trySetState(std::span<TState*>);
  bool tryResetState(TState*); bool tryResetState(std::span<TState*>);
  void forceSetState(TState*); void setAllowNullStates(bool); };

// StateMachine2.h / KeyChange.h
template<class TKey,class TState> class StateMachine<TKey,TState> : public StateMachine<TState> {
  TKey currentKey() const; TState* trySetState(TKey); TState* tryResetState(TKey);
  TState* forceSetState(TKey); TState* getState(TKey) const;
  void add(TKey,TState*); bool remove(TKey); void addRange(std::span<const TKey>,std::span<TState*>);
  void setFakeKey(TKey); };

// WithDefault.h
template<class TState> class StateMachineWithDefault : public StateMachine<TState> { public:
  TState* defaultState() const; void setDefaultState(TState*);
  bool trySetDefaultState(); bool tryResetDefaultState(); void forceSetDefaultState(); };

// InputBuffer.h
template<class TState> class InputBuffer { public:
  InputBuffer(StateMachine<TState>*=nullptr); TState* state() const; bool isActive() const;
  void buffer(TState*,float); bool update(float dt); void clear(); };

// StateBehaviour.h / DelegateState.h
class StateBehaviour : public State { bool isActive() const; void setActive(bool); };
class DelegateState : public IState { public:
  std::function<bool()> canEnter, canExit; std::function<void()> onEnter, onExit; };
```

## jobs（任务调度 / Animation Jobs，详见 16）

```cpp
// jobs/IAnimationJob.h
struct IAnimationJob { public: virtual ~IAnimationJob()=default;
  virtual void processRootMotion(Pose&, const Skeleton&) {}
  virtual void processAnimation(Pose&, const Skeleton&, float deltaTime)=0; };

// jobs/JobScheduler.h
struct JobHandle { uint64_t id=0; bool valid() const; };
class IJobScheduler { public: virtual ~IJobScheduler()=default;
  virtual JobHandle schedule(std::function<void()> work,
                             std::span<const JobHandle> deps={})=0;
  virtual void complete(JobHandle)=0; virtual void waitAll()=0;
  virtual void parallelFor(size_t count,
                           std::function<void(size_t,size_t)> body)=0;
  virtual int workerCount() const=0; };
class InlineJobScheduler : public IJobScheduler { /* 同步执行 */ };

// jobs/ScriptPoseNode.h（实际位于 graph/Nodes.h）
class ScriptPoseNode : public PlayableNode { public:
  IAnimationJob* job=nullptr; IJobScheduler* scheduler=nullptr; };

// jobs/TwoBoneIK.h / Damping.h / SimpleLean.h / AnimatedValue.h
class TwoBoneIKJob : public IAnimationJob { int top,mid,low; Vec3 effectorPosition; Quat effectorRotation; };
class DampingJob  : public IAnimationJob {
  int root; std::vector<int> joints; std::vector<Vec3> positions, velocities;
  void setup(const Skeleton&,const Pose&,int rootBone,std::span<const int> jointBones); };
class SimpleLeanJob : public IAnimationJob { int root; std::vector<int> bones; Vec3 axis; float angle; };
class AnimatedFloat { const Curve* curve; const PropertyChannel* property;
  bool bindProperty(const Clip&, std::string_view path, std::string_view attribute);
  bool bindProperty(const Clip&, std::string_view attribute);
  float sample(float time) const; };
class AnimatedBool  { /* 同上 */ float threshold=0.5f; bool sample(float) const; };
class AnimatedInt   { /* 同上 */ int sample(float) const; };

// AnimancerPlayable（P5 新增）
PlayableHandle insertOutputJob(IAnimationJob* job, IJobScheduler* scheduler=nullptr);
void removePlayable(PlayableHandle handle);
void setRootMotionTarget(IRootMotionTarget*);
void setRootMotionRootBone(int bone);
int rootMotionRootBone() const;
const RootMotionDelta& rootMotionDelta() const;
```

## rootmotion（`include/anim/animancer/rootmotion/`）

```cpp
class IRootMotionTarget { virtual void applyRootMotion(const RootMotionDelta&)=0;
                          virtual bool enabled() const { return true; } };
class RootMotionRedirect { IRootMotionTarget* target; bool applyRootMotion=true;
                           void update(const RootMotionDelta&); };

class RootMotionToTransform : public IRootMotionTarget { Transform* target; };
class ICharacterControllerLike { virtual void move(const Vec3&)=0; virtual void rotate(const Quat&)=0; };
class RootMotionToCharacterController : public IRootMotionTarget { ICharacterControllerLike* controller; };
class IRigidbodyLike { virtual Vec3 position() const=0; virtual Quat rotation() const=0;
                       virtual void movePosition(const Vec3&)=0; virtual void moveRotation(const Quat&)=0; };
class RootMotionToRigidbody : public IRootMotionTarget { IRigidbodyLike* rigidbody; };
```

## clip IO / converter（P6）

```cpp
// clip/ClipIO.h（运行时，格式见 03 §4 version 2）
bool writeClip(const Clip&, Writer&, std::string* error=nullptr);
std::unique_ptr<Clip> readClip(Reader&, std::string* error=nullptr);
std::vector<uint8_t> encodeClip(const Clip&, std::string* error=nullptr);
std::unique_ptr<Clip> decodeClip(Span<const uint8_t>, std::string* error=nullptr);
std::unique_ptr<Clip> decodeClip(const std::vector<uint8_t>&, std::string* error=nullptr);

// tools/anim_converter（离线）
ParsedClip parseAnimationClip(std::string_view yaml, std::string& error);
ConvertResult convertFile(const std::string& animPath, const ConvertOptions&);
bool writeAbFile(const Clip&, const std::string& path, std::string& error);

struct ConvertOptions { const SkeletonAsset* skeleton; const AvatarConfig* avatar; };
struct ConvertResult  { std::unique_ptr<Clip> clip; std::vector<std::string> warnings; std::string error; };
```

CLI：

```
anim_converter --input Walk.anim --output Walk.ab [--skeleton skeleton.json] [--avatar a.json]
anim_converter --batch Examples/ --out-dir out/ [--skeleton ...]
anim_converter --fbx-meta model.fbx.meta [--emit-avatar avatar.json]
```

## integration / manifest（P8.1/P8.4）

```cpp
// integration/AnimationAssets.h
struct ClipEventInfo { float normalizedTime; std::string name; };
struct ClipAssetInfo {
  std::string name, action, file;
  bool looping = false;
  float fadeDuration = 0.25f, speed = 1.0f;
  float normalizedStartTime = std::numeric_limits<float>::quiet_NaN();
  std::string rootBone;
  std::vector<ClipEventInfo> events;
};
struct AnimationSet {
  int version = 1;
  std::string skeletonFile;
  std::string coordinate = "gltf";
  std::vector<ClipAssetInfo> clips;
  const ClipAssetInfo* find(std::string_view) const noexcept;
  ClipAssetInfo* find(std::string_view) noexcept;
};
using EventCallback = std::function<void()>;
using EventCallbackMap = std::unordered_map<std::string, EventCallback>;

// integration/AnimationManifest.h
bool loadAnimationManifest(std::string_view text, AnimationSet&, std::string& error);
bool loadAnimationManifestFile(const std::string& path, AnimationSet&, std::string& error);
```

`loadAnimationManifestFile` 把 `skeleton` 和 `clips[].file` 的相对路径解析到
manifest 所在目录；`loadAnimationManifest` 保持原字符串。

```cpp
// integration/AnimationClipLibrary.h
class AnimationClipLibrary {
 public:
  bool load(const AnimationSet&, std::string& error);
  const Clip* find(std::string_view name) const noexcept;
  const ClipAssetInfo* findInfo(std::string_view name) const noexcept;
};

// integration/AnimationTransitions.h
class AnimationTransitions {
 public:
  bool build(const AnimationClipLibrary&, const EventCallbackMap&, std::string& error);
  ClipTransition* find(std::string_view name) noexcept;
  const ClipAssetInfo* info(std::string_view name) const noexcept;
  AnimancerState* play(AnimancerAnimator&, std::string_view name,
                       std::string* error = nullptr);
  bool bindRootBone(AnimancerAnimator&, std::string_view name,
                    std::string* error = nullptr) const;
};
```

`AnimationClipLibrary` 共享 decode 后的 `Clip`，并以 manifest 的 `loop` 覆盖 `.ab`；
`AnimationTransitions` 是每角色副本，持有自己的 `EventSequence`，`play` 会根据
`rootBone` 设置根运动骨骼。两者都必须比使用它们的 State/Animancer 活得久。

P8.2 的 `anim_pack` 不进入公共运行时库：其公开入口位于
`tools/anim_pack/AnimPack.h`，由 `ANIM_BUILD_ANIM_PACK` 控制构建，核心库仍不依赖 cgltf。

## animancer / retarget（P9，可选）

```cpp
// animancer/Transition.h
class RetargetClipTransition : public ITransition {
 public:
  const Clip* clip = nullptr;                // 源 BoneLocalClip
  const Skeleton* sourceSkeleton = nullptr;
  const HumanoidAvatar* sourceAvatar = nullptr;
  const HumanoidAvatar* targetAvatar = nullptr;
  float fadeDurationValue = 0.25f;
  float speedValue = 1.0f;
  float normalizedStartTime = std::numeric_limits<float>::quiet_NaN();
  EventSequence eventSequence;
};

// animancer/RetargetClipState.h
class RetargetClipState : public AnimancerState {
 public:
  RetargetClipState(AnimancerPlayable*, const Clip&, const Skeleton&,
                    const HumanoidAvatar& source, const HumanoidAvatar& target);
};
```

`RetargetClipTransition` 通过目标 `AnimancerPlayable::skeleton()` 取得目标骨架；源 Clip
必须是 `ClipType::BoneLocal`，内部使用现有 `retargetHumanoidLocalPose`。源
`Skeleton`/两个 `HumanoidAvatar` 以及 Transition（含事件序列）必须在 State 生命周期内有效。

## integration / threading（P7）+ AnimancerAnimator 门面（P8.5/P8.5b）

```cpp
// animancer/AnimancerAnimator.h
void initialize(Skeleton&, const HumanoidAvatar* = nullptr, int layers = 1);
bool isInitialized() const;
void update(float dt);                       // 图求值 + computePalette
void evaluate(float dt);                     // update 别名
void evaluate();
float speed() const;                         // 未初始化返回 1
void setSpeed(float);
bool isGraphPlaying() const;
void pauseGraph();
void unpauseGraph();
const std::vector<Mat4>& palette() const;
RootMotionDelta consumeRootMotion() const;

AnimancerState* play(const Clip&);
AnimancerState* play(const Clip&, float fadeDuration,
                     FadeMode mode = FadeMode::FixedSpeed);
AnimancerState* play(ITransition&);
AnimancerState* play(ITransition&, float fadeDuration,
                     FadeMode mode = FadeMode::FixedSpeed);
AnimancerState* play(AnimancerState*);
AnimancerState* play(AnimancerState*, float fadeDuration,
                     FadeMode mode = FadeMode::FixedSpeed);
AnimancerState* tryPlay(Key);
AnimancerState* tryPlay(Key, float fadeDuration,
                        FadeMode mode = FadeMode::FixedSpeed);
void stop();
AnimancerState* stop(const Clip&);
AnimancerState* stop(Key);
bool isPlaying() const;
bool isPlaying(const Clip&) const;
bool isPlaying(Key) const;
bool isPlayingClip(const Clip&) const;
AnimancerLayer* layer(int index);

// integration/Skinning.h
struct SkinningPalette { std::span<const Mat4> bones; };
class ISkinningUploader { virtual void uploadSkinningPalette(const SkinningPalette&) = 0; };
void transposePalette(const std::vector<Mat4>&, std::vector<Mat4>&);  // glm → bgfx 行主序

// integration/Coordinate.h（左手 ↔ 右手，双次调用=原值）
Mat4 flipZMatrix();
Vec3/Quat/Transform/Mat4 flipZ(...);
Pose flipZ(const Pose&);
void flipZPalette(std::vector<Mat4>&);

// jobs/ThreadPoolJobScheduler.h
ThreadPoolJobScheduler scheduler(workerCount = 0);  // 0 = hardware_concurrency
JobHandle schedule(std::function<void()>, std::span<const JobHandle> deps = {});
void complete(JobHandle);
void waitAll();
void parallelFor(size_t count, std::function<void(size_t, size_t)> body);
int workerCount() const;
```

`AnimancerAnimator` 的未初始化保护：`play(...)`/`layer(...)` 返回 `nullptr`，
`stop()` 无操作，`isPlaying(...)` 返回 `false`。

`play(clip)`/`play(state)`/`tryPlay(key)` 会按已注册状态的原有 Layer 播放（对应
`AnimancerPlayable.GetLocalLayer`），不会强制搬到 Layer 0；若显式让某 Layer
`play(state)` 且 state 属于另一 Layer，则按原 `AnimancerLayer.GetOrCreateState(AnimancerState)`
克隆状态。`isPlayingClip` 搜索所有状态，`isPlaying()`/`isPlaying(clip)` 在图暂停时返回 `false`。

### SoloAnimation（单 Clip 门面）

```cpp
// animancer/SoloAnimation.h
void initialize(Skeleton&, const HumanoidAvatar* = nullptr);
bool play(const Clip&);                    // 不创建 Layer/State，失败返回 false
void stop();                               // 暂停并回到时间 0
void update(float dt);                     // 采样 + computePalette
void setIsPlaying(bool);
void setSpeed(float);                      // 0 会暂停
void setTime(float);
void setNormalizedTime(float);
const Pose& pose() const;
const std::vector<Mat4>& palette() const;
```

`SoloAnimation` 对应原项目 `SoloAnimation`：单 Clip 直接采样，不接事件/根运动；
Humanoid Clip 必须提供 `HumanoidAvatar`，`Skeleton`/`HumanoidAvatar` 与 Clip 需在播放期间有效。
播放中再次 `play(clip)` 会从 0 重新开始；
非循环 Clip 到末尾自动暂停，循环 Clip 的 `time` 保持累计值、采样时间取 `fmod`。

### NamedAnimancerAnimator（名字→Clip）

```cpp
// animancer/NamedAnimancerAnimator.h
struct NamedAnimation { std::string name; const Clip* clip; };
void initialize(Skeleton&, const HumanoidAvatar* = nullptr, int layers = 1);
void setAnimations(std::initializer_list<NamedAnimation>);
bool addAnimation(std::string name, const Clip&);
bool removeAnimation(std::string_view name);
const Clip* findAnimation(std::string_view name) const;
const Clip* defaultAnimation() const;
bool setDefaultAnimation(std::string_view name);
AnimancerState* playDefault();
AnimancerState* start();                   // playAutomatically ? playDefault() : nullptr
AnimancerState* play(std::string_view name);
AnimancerState* play(std::string_view name, float fadeDuration, FadeMode);
AnimancerState* stop(std::string_view name);
bool isPlaying(std::string_view name) const;
```

对应原 `NamedAnimancerComponent`：调用方显式注册名字；默认动画是第一个有效项，
`start()` 对应原 `OnEnable` 的自动播放策略；`playDefault()` 总是显式播放默认项。`Clip` 只有 `StringId name`，因此 C++ 侧
不能像 Unity 一样自动从 `clip.name` 取得字符串名字。

引擎侧每帧：

```cpp
for (auto* a : animators) a->update(dt);      // 或 parallelFor 跨角色
scheduler.waitAll();                          // 上传调色板前
uploadBones(a->palette());
```

> EnTT 适配（`integration/ecs/entt/*`）是可选方案，见 [16](16-任务调度与ECS集成.md)；
> 核心库不包含 EnTT，也不依赖任何渲染头。开启 `ANIM_ENABLE_ENTT` 后可使用：
>
> ```cpp
> #include "anim/integration/ecs/entt/Components.h"
> #include "anim/integration/ecs/entt/Systems.h"
>
> registry.emplace<ecs::AnimancerComponent>(entity, ecs::AnimancerComponent{&animator});
> registry.emplace<ecs::JobListComponent>(entity, ecs::JobListComponent{{&ikJob}});
> registry.emplace<ecs::PoseComponent>(entity);
> registry.emplace<ecs::SkinningComponent>(entity);
> ecs::bindAnimationJobs(registry);       // job 插入图输出链，顺序执行
> ecs::animationGraphSystem(registry, dt);
> ecs::syncAnimationOutputs(registry);    // 可选：Pose/palette/rootMotion 副本
> ```


---

## 约定

- `Key` = 状态字典键（可 `int64` id / 指针 / `StringId`，用 variant 或统一 `uint64`）。
- `NaN` 用 `std::numeric_limits<float>::quiet_NaN()`。
- `IUpdatable`：`virtual void update() = 0;`（对齐原项目），由 pre/post 列表驱动。
- `AvatarMask`：`struct AvatarMask { std::vector<float> boneWeights; };`（按骨索引）。
- 核心库不绑定 ECS；EnTT 仅出现在 `integration/ecs/entt/*` 且受 `ANIM_ENABLE_ENTT` 保护。
