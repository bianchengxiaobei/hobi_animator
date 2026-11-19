#include "anim/hobi/ExitEvent.h"

#include <utility>

#include "anim/hobi/HobiNode.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/core/Assert.h"
#include "anim/core/Pool.h"

namespace anim {
namespace {

Pool<ExitEvent>& exitEventPool() {
    static Pool<ExitEvent> pool;
    return pool;
}

}  // namespace

ExitEvent* ExitEvent::registerEvent(HobiNode& node, Callback callback) {
    HobiPlayable* root = node.root();
    ANIM_CHECK_MSG(root != nullptr, "Node is not connected to an HobiPlayable");

    ExitEvent* exit = exitEventPool().acquire();
    exit->_node = &node;
    exit->_root = root;
    exit->_callback = std::move(callback);
    root->requirePostUpdate(exit);
    return exit;
}

bool ExitEvent::unregister(HobiPlayable& playable) {
    for (int i = playable.postUpdatableCount() - 1; i >= 0; --i) {
        auto* exit = dynamic_cast<ExitEvent*>(playable.postUpdatable(i));
        if (exit == nullptr) continue;

        playable.cancelPostUpdate(exit);
        exit->release();
        return true;
    }

    return false;
}

bool ExitEvent::unregister(HobiNode& node) {
    HobiPlayable* root = node.root();
    if (root == nullptr) return false;

    for (int i = root->postUpdatableCount() - 1; i >= 0; --i) {
        auto* exit = dynamic_cast<ExitEvent*>(root->postUpdatable(i));
        if (exit == nullptr || exit->_node != &node) continue;

        root->cancelPostUpdate(exit);
        exit->release();
        return true;
    }

    return false;
}

int ExitEvent::unregisterAll(HobiNode& node) {
    HobiPlayable* root = node.root();
    if (root == nullptr) return 0;

    int count = 0;
    for (int i = root->postUpdatableCount() - 1; i >= 0; --i) {
        auto* exit = dynamic_cast<ExitEvent*>(root->postUpdatable(i));
        if (exit == nullptr || exit->_node != &node) continue;

        root->cancelPostUpdate(exit);
        exit->release();
        ++count;
    }
    return count;
}

bool ExitEvent::isActive() const noexcept {
    return _root != nullptr && _root->isPostUpdating(this);
}

void ExitEvent::update(bool& needsMoreUpdates) {
    needsMoreUpdates = false;

    if (_node != nullptr && _node->effectiveWeight() > 0.0f) return;

    // 先取消自身再回调：回调里可能销毁节点、注册新事件或触发其它更新。
    HobiPlayable* root = _root;
    Callback callback = std::move(_callback);
    if (root != nullptr) root->cancelPostUpdate(this);
    _root = nullptr;
    _node = nullptr;

    if (callback) callback();
    exitEventPool().release(this);
}

void ExitEvent::release() {
    _node = nullptr;
    _root = nullptr;
    _callback = nullptr;
    exitEventPool().release(this);
}

}  // namespace anim
