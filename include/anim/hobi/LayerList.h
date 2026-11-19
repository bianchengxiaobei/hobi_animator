#pragma once

#include <memory>
#include <vector>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/graph/PlayableHandle.h"

// 层列表（对应 HobiPlayable.LayerList）。
//
// 文档：docs/07-Hobi门面.md §5
namespace anim {

class HobiPlayable;
class HobiLayer;
struct AvatarMask;
class PlayableGraph;

class LayerList : public IAnimationClipCollection {
public:
    explicit LayerList(HobiPlayable& root, PlayableHandle layerMixerNode);
    ~LayerList();

    int count() const noexcept { return static_cast<int>(_layers.size()); }
    void setCount(int count);
    int capacity() const noexcept { return _capacity; }
    void setCapacity(int capacity);

    HobiLayer* add();
    HobiLayer* operator[](int index) const { return getLayer(index); }
    HobiLayer* getLayer(int index) const;

    bool isAdditive(int layer) const;
    void setAdditive(int layer, bool additive);
    void setMask(int layer, const AvatarMask* mask);
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

    static int defaultCapacity;

private:
    HobiPlayable* _root = nullptr;
    PlayableHandle _layerMixerNode{};
    std::vector<std::unique_ptr<HobiLayer>> _layers;
    int _capacity = 0;
};

}  // namespace anim
