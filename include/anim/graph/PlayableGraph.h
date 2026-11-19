#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "anim/graph/PlayableHandle.h"
#include "anim/graph/PlayableNode.h"

// Playable 图：槽位数组 + 空闲栈 + 版本号；compile() 校验并给出拓扑序，
// evaluate() 做两阶段求值。
//
// 文档：docs/06-Playable图.md §4
// 编译/诊断/错误累积模式参考 E:\Learn\hdrp-bgfx\src\render\graph\render_graph.{h,cpp}：
//  - compile() 返回 bool，首个错误存 lastError()
//  - 拓扑校验失败不会抛异常
namespace anim {

struct PlayableGraphDiagnostics {
    uint32_t nodeCount = 0;       // 存活的节点数（含不可达）
    uint32_t reachableNodes = 0;  // 从输出根可达的节点数
    uint32_t maxDepth = 0;        // 从根出发的最大深度
};

class PlayableGraph {
public:
    PlayableGraph() = default;
    ~PlayableGraph();

    PlayableGraph(const PlayableGraph&) = delete;
    PlayableGraph& operator=(const PlayableGraph&) = delete;

    // ---- 创建/销毁 ----
    template <typename T, typename... Args>
    PlayableHandle create(Args&&... args) {
        static_assert(std::is_base_of_v<PlayableNode, T>, "T must derive from PlayableNode");
        const uint32_t slotIndex = acquireSlot();
        Slot& slot = *slotStorage(slotIndex);
        slot.node = std::make_unique<T>(std::forward<Args>(args)...);
        const PlayableHandle handle{slotIndex, slot.version};
        slot.node->attach(this, handle, slotIndex);
        slot.node->onPlayableCreate(*this, handle);
        markDirty();
        return handle;
    }

    void destroy(PlayableHandle node);
    void destroySubgraph(PlayableHandle node) { destroy(node); }

    // ---- 连接 ----
    void connect(PlayableHandle source, int sourcePort, PlayableHandle destination, int destinationPort);
    void disconnect(PlayableHandle destination, int destinationPort);
    void setInputCount(PlayableHandle node, int count);
    int getInputCount(PlayableHandle node) const;
    void setInputWeight(PlayableHandle node, int port, float weight);
    float getInputWeight(PlayableHandle node, int port) const;

    // ---- 输出 ----
    void setOutput(PlayableHandle root);
    PlayableHandle output() const noexcept { return _root; }
    PlayableNode* get(PlayableHandle handle) const noexcept;
    PlayableNode* root() const noexcept { return get(_root); }

    // ---- 播放/求值 ----
    void play();
    void stop();
    bool isPlaying() const noexcept { return _playing; }
    void evaluate(float deltaTime);

    // ---- 编译 ----
    bool compile();
    bool compiled() const noexcept { return _compiled; }
    bool dirty() const noexcept { return _dirty; }
    const std::string& lastError() const noexcept { return _error; }
    const PlayableGraphDiagnostics& diagnostics() const noexcept { return _diagnostics; }
    uint32_t frameId() const noexcept { return _frameId; }

    int liveNodeCount() const noexcept;
    std::vector<PlayableHandle> nodeHandles() const;

private:
    friend class PlayableNode;

    struct Slot {
        std::unique_ptr<PlayableNode> node;
        uint32_t version = 0;
    };

    Slot* slotStorage(uint32_t index);
    const Slot* slotStorage(uint32_t index) const;

    uint32_t acquireSlot();
    void releaseSlot(uint32_t index) noexcept;
    void markDirty() noexcept { _dirty = true; }
    void recordError(std::string message);
    bool validateReachable(PlayableHandle root, std::vector<uint32_t>& reachable);
    bool buildOrder(const std::vector<uint32_t>& reachable);

    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeSlots;

    PlayableHandle _root{};
    std::vector<PlayableHandle> _order;         // prepare：父 -> 子
    std::vector<PlayableHandle> _processOrder;  // process：子 -> 父

    PlayableGraphDiagnostics _diagnostics{};
    std::string _error;
    uint32_t _frameId = 0;
    bool _compiled = false;
    bool _dirty = true;
    bool _playing = true;
};

}  // namespace anim
