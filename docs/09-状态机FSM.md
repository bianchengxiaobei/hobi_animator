# 09 · 状态机 FSM

L6 层：独立于动画的通用有限状态机。原项目：`Utilities/FSM/*`（独立 asmdef `Animancer.FSM`）。本库作为 `anim::fsm` 命名空间移植，可单独使用。

> 与原项目一样，FSM **不依赖**动画系统；动画只是它的一种 `IState` 实现。

## 1. IState / State

原项目：`Utilities/FSM/IState.cs`。

```cpp
struct IState {
    virtual ~IState() = default;
    virtual bool canEnterState() const = 0;
    virtual bool canExitState() const = 0;
    virtual void onEnterState() = 0;
    virtual void onExitState() = 0;
};

// 全默认实现
class State : public IState {
public:
    bool canEnterState() const override { return true; }
    bool canExitState()  const override { return true; }
    void onEnterState() override {}
    void onExitState()  override {}
};

// 知道所属状态机（用于扩展方法 TryEnterState 等）
template <class TState>
struct IOwnedState : IState { virtual StateMachine<TState>* ownerStateMachine() = 0; };
```

`StateMachine<TState>` 要求 `TState : IState`（原为 `class, IState`）。

## 2. StateChange（当前转换上下文）

原项目：`Utilities/FSM/StateChange.cs`。用 `thread_local` 维护“正在发生的状态切换”，供 `GetPreviousState/GetNextState` 读取，且支持递归切换（RAII 恢复）。

```cpp
template <class TState>
struct StateChange {
    static bool isActive();
    static StateMachine<TState>* stateMachine();
    static TState* previousState();
    static TState* nextState();

    // RAII：构造时把当前上下文压栈，析构时恢复
    StateChange(StateMachine<TState>*, TState* previous, TState* next);
    ~StateChange();
};
```
- 原实现用 `struct + IDisposable + using`（`StateChange.cs:84-106`）。本库用 RAII。
- 线程安全：原注释称 thread-safe（`thread_local`）。

## 3. StateMachine\<TState\>（keyless）

原项目：`Utilities/FSM/StateMachine1.cs`。

```cpp
template <class TState>
class StateMachine {
public:
    TState* currentState() const;
    TState* previousState() const;   // StateChange<TState>::previousState()
    TState* nextState() const;

    void initializeAfterDeserialize();          // 反序列化后进入当前状态

    bool canSetState(TState*);
    TState* canSetState(std::span<TState*>);
    bool trySetState(TState*);                  // 已是当前状态则直接 true
    bool trySetState(std::span<TState*>);
    bool tryResetState(TState*);                // 不检查是否已当前
    bool tryResetState(std::span<TState*>);
    void forceSetState(TState*);                // 不检查 CanEnter/CanExit，直接切换

    void setAllowNullStates(bool);              // debug
};
```

精确语义（`StateMachine1.cs`）：
- `canSetState(state)`：在 `StateChange` 上下文内，检查 `current->canExitState()` 与 `state->canEnterState()` 都为真（`:196-213`）。
- `trySetState(state)`：若 `current==state` 直接返回 true；否则 `tryResetState`（`:245-258`）。
- `tryResetState(state)`：`canSetState ? forceSetState : false`（`:284-291`）。
- `forceSetState(state)`：在 `StateChange` 上下文内 `current->onExitState(); current=state; state->onEnterState()`（`:320-345`）。
- 批量版本按顺序尝试第一个成功者（`:222-236,267-275`）。

## 4. StateMachine\<TKey,TState\>（带 key，等价 StateMachine2）

原项目：`Utilities/FSM/StateMachine2.cs`。

- 内部持有一个 `Dictionary<TKey,TState>`，可按 key 注册/查询/移除状态。
- 额外 `trySetState(key, state)`、`tryResetState(key)`、`forceSetState(key,...)`、`getState(key)`、`addRange(keys,states)`、`setFakeKey(key)`（`StateMachine2.cs:96-358`）。
- 用法：状态预先注册，按 key 驱动。

## 5. WithDefault（默认状态）

原项目：`Utilities/FSM/StateMachine1.WithDefault.cs`。

```cpp
template <class TState>
class StateMachineWithDefault : public StateMachine<TState> {
public:
    TState* defaultState() const;
    void setDefaultState(TState*);      // 若当前为空则立即 forceSetState
    bool trySetDefaultState();
    bool tryResetDefaultState();
    Action forceSetDefaultState;        // 缓存委托（事件里用）
};
```
- `initializeAfterDeserialize`：若当前为空则进入 `defaultState`（`:73-90`）。

## 6. InputBuffer（输入缓冲）

原项目：`Utilities/FSM/StateMachine1.InputBuffer.cs`。

```cpp
template <class TStateMachine>
class InputBuffer {
public:
    TStateMachine* stateMachine;
    TState* state = nullptr;
    float timeOut = 0;
    bool isActive() const { return state != nullptr; }
    void buffer(TState*, float timeOut);
    bool update(float deltaTime);   // 尝试进入；失败则倒计时；超时清空
    void clear();
};
```
- `update`：`tryEnterState() ? (clear,true) : (timeOut-=dt; timeOut<0 ? clear : -) `（`:137-156`）。
- 典型用途：攻击/跳跃输入提前缓冲，等角色可行动时立即进入。

## 7. 便捷扩展（StateExtensions）

原项目：`IState.cs:189-417`。提供 `isCurrentState / tryEnterState / tryReEnterState / forceEnterState / getPreviousState / getNextState`。本库用自由函数模板实现。

## 8. StateBehaviour / DelegateState

- `StateBehaviour`（原 `StateBehaviour.cs`）：MonoBehaviour 状态，`onEnterState` 置 `enabled=true`，`onExitState` 置 `enabled=false`。C++ 版提供 `StateBehaviourBase`（可选 `active` 标志 + 回调），供引擎中“状态组件”使用。
- `DelegateState`（原 `DelegateState.cs`）：用 `std::function` 组装 `canEnter/canExit/onEnter/onExit`，便于快速搭建。

```cpp
class DelegateState : public IState {
public:
    std::function<bool()> canEnter, canExit;
    std::function<void()> onEnter, onExit;
    // 未设置视为 true / 空操作
};
```

## 9. 与动画的结合方式

- 动画状态通常是 `StateBehaviour`：进入时 `layer->play(transition)`；退出时做清理。
- 也可用 `DelegateState`：`onEnter = [&]{ animancer->play(attack); }`。
- FSM 与动画解耦意味着：可以只做纯逻辑状态机，也可以驱动动画。

## 10. 文件清单

```
include/anim/animancer/fsm/IState.h
include/anim/animancer/fsm/StateChange.h
include/anim/animancer/fsm/StateMachine.h         // keyless
include/anim/animancer/fsm/StateMachine2.h        // keyed
include/anim/animancer/fsm/WithDefault.h
include/anim/animancer/fsm/InputBuffer.h
include/anim/animancer/fsm/StateBehaviour.h
include/anim/animancer/fsm/DelegateState.h
```

## 11. 与原项目对照校验点

1. `canSetState` 必须同时检查当前状态的 `canExitState` 与目标的 `canEnterState`。
2. `trySetState` 与 `tryResetState` 的区别（是否允许重入当前状态）。
3. `StateChange` 的递归恢复语义。
4. `WithDefault` 在反序列化后自动进入默认状态。
5. `InputBuffer` 的超时行为（`TimeOut < 0` 才清空，恰好为 0 仍有效）。

## 12. 实现状态（P5）

已实现（`include/anim/animancer/fsm/`，头文件模板）：

| 文件 | 内容 |
|---|---|
| `IState.h` | `IState` / `State` / `IOwnedState` + 便捷自由函数 |
| `StateChange.h` | `thread_local` 栈式上下文（RAII，支持递归切换） |
| `StateMachine.h` | keyless `StateMachine<TState>` |
| `StateMachine2.h` | keyed `StateMachine<TKey,TState>`（partial specialization） |
| `KeyChange.h` | `KeyChange<TKey>` 递归上下文 |
| `WithDefault.h` | `StateMachineWithDefault<TState>` |
| `InputBuffer.h` | 输入缓冲与超时 |
| `StateBehaviour.h` | 组件式状态（active + onEnable/onDisable） |
| `DelegateState.h` | `std::function` 组装状态 |

与原 C# 的差异（已记录）：

- C# 的 `ArgumentNullException`/`InvalidOperationException` 在 C++ 中改为
  `ANIM_CHECK`（`std::logic_error`）；Release 下也保留检查。
- `IOwnedState` 不继承 `IState`（避免 C++ 多重继承的二义性），
  由使用方同时继承 `State` 与 `IOwnedState<TState>`。
- 批量接口用 `std::span<TState*>` 替代 `IList<TState>`。
- `StateChange` 是 RAII 类（不可拷贝），析构自动恢复外层上下文。

测试：`tests/test_fsm.cpp`（11 个用例），覆盖转换/守卫/批量/递归上下文/
默认状态/输入缓冲/带 key 状态机/扩展函数/DelegateState/StateBehaviour。

