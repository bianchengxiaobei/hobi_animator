#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

#include "anim/core/Assert.h"

// 简单对象池：避免热路径每帧分配。
//
// 文档：docs/02-核心数据类型.md §3
// 对齐原项目 Internal/Collections/ObjectPool.cs：
//  - Acquire(): 优先复用空闲对象，否则新建。
//  - Release(): 断言非空后归还。
//  - 后进先出（原项目取 Items[Count-1] 并 RemoveAt）。
//
// 非线程安全：多线程场景由调用方每线程持有一个池（见 12）。
namespace anim {

template <typename T>
class Pool {
public:
    static_assert(std::is_default_constructible_v<T>,
                  "Pool<T> requires a default-constructible T");

    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    // 取一个对象：优先复用空闲对象，否则新建。
    T* acquire() {
        if (!_free.empty()) {
            T* item = _free.back();
            _free.pop_back();
            return item;
        }
        _all.push_back(std::make_unique<T>());
        return _all.back().get();
    }

    // 归还对象。调用方负责在归还前把对象恢复到可复用状态。
    void release(T* item) {
        ANIM_CHECK(item != nullptr);
        _free.push_back(item);
    }

    // 预分配空闲对象，使后续 acquire 不触发分配。
    void reserve(std::size_t count) {
        _all.reserve(_all.size() + count);
        _free.reserve(_free.size() + count);
        while (_free.size() < count) {
            _all.push_back(std::make_unique<T>());
            _free.push_back(_all.back().get());
        }
    }

    std::size_t available() const noexcept { return _free.size(); }
    std::size_t size() const noexcept { return _all.size(); }
    std::size_t capacity() const noexcept { return _all.capacity(); }

    void clear() {
        _free.clear();
        _all.clear();
    }

private:
    std::vector<std::unique_ptr<T>> _all;
    std::vector<T*> _free;
};

}  // namespace anim
