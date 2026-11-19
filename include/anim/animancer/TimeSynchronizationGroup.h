#pragma once

#include <optional>
#include <unordered_set>

#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/StateDictionary.h"

// 按 key 分组的相位同步（对应 Utilities/TimeSynchronizationGroup.cs）。
//
// C# 用 HashSet<object> 存 key；C++ 侧使用 const void*，可直接传 Clip* /
// AnimancerState* / 其它对象的地址。每次 StoreTime 只会在 key 属于本组时
// 记录 NormalizedTime，SyncTime 再把时间应用到目标状态。
namespace anim {

class TimeSynchronizationGroup {
public:
    using Key = const void*;

    explicit TimeSynchronizationGroup(AnimancerPlayable* animancer = nullptr);

    AnimancerPlayable* animancer() const noexcept { return _animancer; }
    void setAnimancer(AnimancerPlayable* animancer);

    std::optional<float> normalizedTime;

    void add(Key key) { _keys.insert(key); }
    bool remove(Key key) { return _keys.erase(key) != 0; }
    bool contains(Key key) const { return _keys.find(key) != _keys.end(); }
    void clear() { _keys.clear(); }
    std::size_t size() const noexcept { return _keys.size(); }

    bool storeTime(Key key);
    bool storeTime(Key key, AnimancerState* state);

    bool syncTime(Key key);
    bool syncTime(Key key, float deltaTime);
    bool syncTime(Key key, AnimancerState* state);
    bool syncTime(Key key, AnimancerState* state, float deltaTime);

private:
    float deltaTimeOf(const AnimancerState* state) const;

    AnimancerPlayable* _animancer = nullptr;
    std::unordered_set<Key> _keys;
};

}  // namespace anim
