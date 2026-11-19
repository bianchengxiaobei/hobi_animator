#include "anim/graph/PlayableGraph.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "anim/core/Assert.h"

namespace anim {

PlayableGraph::~PlayableGraph() {
    for (Slot& slot : _slots) {
        if (slot.node) {
            slot.node->onPlayableDestroy();
            slot.node.reset();
        }
    }
}

PlayableGraph::Slot* PlayableGraph::slotStorage(uint32_t index) {
    ANIM_CHECK_MSG(index < _slots.size(), "slot index out of range");
    return &_slots[index];
}

const PlayableGraph::Slot* PlayableGraph::slotStorage(uint32_t index) const {
    ANIM_CHECK_MSG(index < _slots.size(), "slot index out of range");
    return &_slots[index];
}

uint32_t PlayableGraph::acquireSlot() {
    if (!_freeSlots.empty()) {
        const uint32_t index = _freeSlots.back();
        _freeSlots.pop_back();
        return index;
    }
    const uint32_t index = static_cast<uint32_t>(_slots.size());
    _slots.emplace_back();
    return index;
}

void PlayableGraph::releaseSlot(uint32_t index) noexcept {
    if (index >= _slots.size()) return;
    Slot& slot = _slots[index];
    slot.node.reset();
    ++slot.version;
    _freeSlots.push_back(index);
}

PlayableNode* PlayableGraph::get(PlayableHandle handle) const noexcept {
    if (!handle.valid() || handle.index >= _slots.size()) return nullptr;
    const Slot& slot = _slots[handle.index];
    if (!slot.node || slot.version != handle.version) return nullptr;
    return slot.node.get();
}

void PlayableGraph::recordError(std::string message) {
    if (_error.empty()) _error = std::move(message);
}

// ---------------------------------------------------------------------------
// 创建 / 销毁
// ---------------------------------------------------------------------------

void PlayableGraph::destroy(PlayableHandle handle) {
    PlayableNode* node = get(handle);
    if (node == nullptr) return;

    // 先记录子节点（销毁本节点后无法再读）。
    std::vector<PlayableHandle> children;
    children.reserve(static_cast<std::size_t>(node->inputCount()));
    for (int port = 0; port < node->inputCount(); ++port) {
        const PlayableHandle child = node->input(port);
        if (child.valid()) children.push_back(child);
    }

    // 从所有父节点断开。
    for (Slot& slot : _slots) {
        if (!slot.node || slot.node.get() == node) continue;
        for (int port = 0; port < slot.node->inputCount(); ++port) {
            if (slot.node->input(port) == handle) {
                slot.node->setInputHandle(port, PlayableHandle{});
            }
        }
    }

    node->onPlayableDestroy();
    node->detach();

    if (_root == handle) _root = PlayableHandle{};
    releaseSlot(handle.index);
    markDirty();

    // 递归销毁不再被任何节点引用的子节点。
    auto isReferenced = [this](PlayableHandle candidate) {
        for (const Slot& slot : _slots) {
            if (!slot.node) continue;
            for (int port = 0; port < slot.node->inputCount(); ++port) {
                if (slot.node->input(port) == candidate) return true;
            }
        }
        return false;
    };

    for (PlayableHandle child : children) {
        if (get(child) == nullptr) continue;
        if (!isReferenced(child)) destroy(child);
    }
}

// ---------------------------------------------------------------------------
// 连接
// ---------------------------------------------------------------------------

void PlayableGraph::connect(PlayableHandle source, int sourcePort, PlayableHandle destination,
                            int destinationPort) {
    PlayableNode* destinationNode = get(destination);
    ANIM_CHECK_MSG(destinationNode != nullptr, "destination playable is invalid");
    ANIM_CHECK_MSG(get(source) != nullptr, "source playable is invalid");
    ANIM_CHECK_MSG(sourcePort >= 0, "source port must be non-negative");
    ANIM_CHECK_MSG(destinationPort >= 0, "destination port must be non-negative");

    // 本库节点只有单输出，sourcePort 仅为接口兼容保留。
    (void)sourcePort;
    destinationNode->setInputHandle(destinationPort, source);
    markDirty();
}

void PlayableGraph::disconnect(PlayableHandle destination, int destinationPort) {
    PlayableNode* node = get(destination);
    if (node == nullptr) return;
    ANIM_CHECK_MSG(destinationPort >= 0 && destinationPort < node->inputCount(),
                   "destination port out of range");
    node->setInputHandle(destinationPort, PlayableHandle{});
    markDirty();
}

void PlayableGraph::setInputCount(PlayableHandle nodeHandle, int count) {
    PlayableNode* node = get(nodeHandle);
    ANIM_CHECK_MSG(node != nullptr, "playable is invalid");
    node->resizeInputs(count);
    markDirty();
}

int PlayableGraph::getInputCount(PlayableHandle nodeHandle) const {
    const PlayableNode* node = get(nodeHandle);
    ANIM_CHECK_MSG(node != nullptr, "playable is invalid");
    return node->inputCount();
}

void PlayableGraph::setInputWeight(PlayableHandle nodeHandle, int port, float weight) {
    PlayableNode* node = get(nodeHandle);
    ANIM_CHECK_MSG(node != nullptr, "playable is invalid");
    ANIM_CHECK_MSG(port >= 0, "port must be non-negative");
    ANIM_CHECK_MSG(std::isfinite(weight), "input weight must be finite");
    ANIM_CHECK_MSG(weight >= 0.0f, "input weight must be non-negative");

    if (port >= node->inputCount()) node->resizeInputs(port + 1);
    node->_inputWeights[static_cast<std::size_t>(port)] = weight;
}

float PlayableGraph::getInputWeight(PlayableHandle nodeHandle, int port) const {
    const PlayableNode* node = get(nodeHandle);
    ANIM_CHECK_MSG(node != nullptr, "playable is invalid");
    return node->inputWeight(port);
}

// ---------------------------------------------------------------------------
// 输出 / 播放
// ---------------------------------------------------------------------------

void PlayableGraph::setOutput(PlayableHandle root) {
    _root = root;
    markDirty();
}

void PlayableGraph::play() {
    _playing = true;
    for (Slot& slot : _slots) {
        if (slot.node) slot.node->play();
    }
}

void PlayableGraph::stop() {
    _playing = false;
    for (Slot& slot : _slots) {
        if (slot.node) slot.node->pause();
    }
}

// ---------------------------------------------------------------------------
// 编译
// ---------------------------------------------------------------------------

bool PlayableGraph::validateReachable(PlayableHandle root, std::vector<uint32_t>& reachable) {
    std::vector<uint8_t> visited(_slots.size(), 0);

    struct Item {
        PlayableHandle handle;
        uint32_t depth = 0;
    };
    std::vector<Item> stack;
    stack.push_back(Item{root, 0});

    while (!stack.empty()) {
        const Item item = stack.back();
        stack.pop_back();

        PlayableNode* node = get(item.handle);
        if (node == nullptr) {
            recordError("playable graph references a destroyed node");
            return false;
        }
        if (visited[item.handle.index] != 0) continue;
        visited[item.handle.index] = 1;
        reachable.push_back(item.handle.index);
        _diagnostics.maxDepth = std::max(_diagnostics.maxDepth, item.depth);

        for (int port = 0; port < node->inputCount(); ++port) {
            const PlayableHandle child = node->input(port);
            if (!child.valid()) continue;
            if (get(child) == nullptr) {
                recordError("node '" + node->name() + "' references a destroyed input");
                return false;
            }
            stack.push_back(Item{child, item.depth + 1});
        }
    }
    return true;
}

bool PlayableGraph::buildOrder(const std::vector<uint32_t>& reachable) {
    std::vector<uint8_t> inReachable(_slots.size(), 0);
    for (uint32_t index : reachable) inReachable[index] = 1;

    std::vector<uint32_t> indegree(_slots.size(), 0);
    std::vector<std::vector<uint32_t>> children(_slots.size());

    for (uint32_t index : reachable) {
        PlayableNode* node = _slots[index].node.get();
        for (int port = 0; port < node->inputCount(); ++port) {
            const PlayableHandle child = node->input(port);
            if (!child.valid()) continue;
            if (get(child) == nullptr || !inReachable[child.index]) {
                recordError("node '" + node->name() + "' references an unreachable input");
                return false;
            }
            children[index].push_back(child.index);
            ++indegree[child.index];
        }
    }

    std::vector<uint32_t> queue;
    queue.reserve(reachable.size());
    for (uint32_t index : reachable) {
        if (indegree[index] == 0) queue.push_back(index);
    }

    std::vector<PlayableHandle> order;
    order.reserve(reachable.size());
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const uint32_t index = queue[head];
        order.push_back(PlayableHandle{index, _slots[index].version});
        for (uint32_t child : children[index]) {
            if (--indegree[child] == 0) queue.push_back(child);
        }
    }

    if (order.size() != reachable.size()) {
        recordError("playable graph contains a cycle");
        return false;
    }

    _order = order;
    _processOrder.assign(order.rbegin(), order.rend());
    return true;
}

bool PlayableGraph::compile() {
    _compiled = false;
    _error.clear();
    _order.clear();
    _processOrder.clear();
    _diagnostics = {};

    for (const Slot& slot : _slots) {
        if (slot.node) ++_diagnostics.nodeCount;
    }

    if (!_root.valid()) {
        // 没有输出根：合法，但什么也不求值。
        _compiled = true;
        _dirty = false;
        return true;
    }

    if (get(_root) == nullptr) {
        recordError("output root is invalid");
        return false;
    }

    std::vector<uint32_t> reachable;
    reachable.reserve(_diagnostics.nodeCount);
    if (!validateReachable(_root, reachable)) return false;

    _diagnostics.reachableNodes = static_cast<uint32_t>(reachable.size());
    if (!buildOrder(reachable)) return false;

    _compiled = true;
    _dirty = false;
    return true;
}

void PlayableGraph::evaluate(float deltaTime) {
    if (!_playing) return;
    ANIM_CHECK_MSG(std::isfinite(deltaTime), "deltaTime must be finite");

    if (!_compiled || _dirty) {
        if (!compile()) return;
    }

    ++_frameId;

    // 早期阶段：父 -> 子。prepare 期间允许改图，失效节点会被 get() 过滤。
    for (PlayableHandle handle : _order) {
        PlayableNode* node = get(handle);
        if (node != nullptr) node->prepareFrame(deltaTime);
    }

    // prepare 中改图后重新编译（Hobi 的动态图依赖此行为）。
    if (_dirty) {
        if (!compile()) return;
    }

    // 求值阶段：子 -> 父。节点只读取已算好的输入输出。
    for (PlayableHandle handle : _processOrder) {
        PlayableNode* node = get(handle);
        if (node != nullptr) node->processFrame(deltaTime);
    }
}

int PlayableGraph::liveNodeCount() const noexcept {
    int count = 0;
    for (const Slot& slot : _slots) {
        if (slot.node) ++count;
    }
    return count;
}

std::vector<PlayableHandle> PlayableGraph::nodeHandles() const {
    std::vector<PlayableHandle> handles;
    handles.reserve(_slots.size());
    for (uint32_t index = 0; index < _slots.size(); ++index) {
        if (_slots[index].node) {
            handles.push_back(PlayableHandle{index, _slots[index].version});
        }
    }
    return handles;
}

}  // namespace anim
