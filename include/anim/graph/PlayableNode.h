#pragma once

#include <string>
#include <vector>

#include "anim/graph/PlayableHandle.h"
#include "anim/skeleton/Pose.h"

// Playable 图节点基类。
//
// 文档：docs/06-Playable图.md §3
//
// 求值模型（与 HDRP RenderGraph 的 compile/execute 类似）：
//  - prepareFrame(dt)：父 -> 子（编译出的拓扑序）
//  - processFrame(dt)：子 -> 父（拓扑序反转），节点只读取输入已算好的 output
namespace anim {

class PlayableGraph;

class PlayableNode {
public:
    PlayableNode() = default;
    virtual ~PlayableNode() = default;

    PlayableNode(const PlayableNode&) = delete;
    PlayableNode& operator=(const PlayableNode&) = delete;

    // ---- 身份 ----
    PlayableGraph* graph() const noexcept { return _graph; }
    PlayableHandle handle() const noexcept { return _handle; }
    uint32_t index() const noexcept { return _index; }
    virtual PlayableNodeType type() const noexcept { return PlayableNodeType::Script; }
    virtual const char* typeName() const noexcept { return "Script"; }

    const std::string& name() const noexcept { return _name; }
    void setName(std::string name) { _name = std::move(name); }

    // ---- 生命周期（对齐 Unity PlayableBehaviour）----
    virtual void onPlayableCreate(PlayableGraph& /*graph*/, PlayableHandle /*self*/) {}
    virtual void onPlayableDestroy() {}

    // ---- 双阶段 ----
    // 默认实现只推进时间；子类可扩展（例如事件、权重更新）。
    virtual void prepareFrame(float deltaTime);
    virtual void processFrame(float /*deltaTime*/) {}

    // ---- 位姿输出 ----
    virtual bool hasPose() const noexcept { return false; }
    virtual const Pose& outputPose() const;

    // ---- 输入口 ----
    int inputCount() const noexcept { return static_cast<int>(_inputs.size()); }
    PlayableHandle input(int port) const;
    PlayableNode* inputNode(int port);
    const PlayableNode* inputNode(int port) const;
    float inputWeight(int port) const;

    // ---- 权重/时间/播放 ----
    float weight() const noexcept { return _weight; }
    void setWeight(float weight);

    float speed() const noexcept { return _speed; }
    void setSpeed(float speed);

    double time() const noexcept { return _time; }
    void setTime(double time) { _time = time; }

    double duration() const noexcept { return _duration; }
    void setDuration(double duration) { _duration = duration; }

    bool isLooping() const noexcept { return _looping; }
    void setLooping(bool looping) noexcept { _looping = looping; }

    // 默认 true：prepareFrame 自动推进 time。
    // HobiState 自己管理时间时应设为 false，避免双重推进。
    bool autoAdvanceTime() const noexcept { return _autoAdvanceTime; }
    void setAutoAdvanceTime(bool value) noexcept { _autoAdvanceTime = value; }

    bool isPlaying() const noexcept { return _playing; }
    void play() { _playing = true; }
    void pause() { _playing = false; }

    // 有效权重 > 0 且处于播放态。
    bool isActive() const noexcept { return _playing && _weight > 0.0f; }

protected:
    friend class PlayableGraph;

    void attach(PlayableGraph* graph, PlayableHandle handle, uint32_t index) noexcept;
    void detach() noexcept;

    void setInputHandle(int port, PlayableHandle handle);
    void resizeInputs(int count);

    // 子类写出的位姿。
    Pose _output;

private:
    PlayableGraph* _graph = nullptr;
    PlayableHandle _handle{};
    uint32_t _index = 0;
    std::string _name;

    std::vector<PlayableHandle> _inputs;
    std::vector<float> _inputWeights;

    float _weight = 1.0f;
    float _speed = 1.0f;
    double _time = 0.0;
    double _duration = 0.0;
    bool _looping = false;
    bool _playing = true;
    bool _autoAdvanceTime = true;
};

}  // namespace anim
