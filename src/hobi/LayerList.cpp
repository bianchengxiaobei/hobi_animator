#include "anim/hobi/LayerList.h"

#include <algorithm>

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/core/Assert.h"
#include "anim/graph/Nodes.h"

namespace anim {

int LayerList::defaultCapacity = 4;

LayerList::LayerList(HobiPlayable& root, PlayableHandle layerMixerNode)
    : _root(&root), _layerMixerNode(layerMixerNode), _capacity(defaultCapacity) {}

LayerList::~LayerList() = default;

void LayerList::setCapacity(int capacity) {
    _capacity = std::max(capacity, count());
}

void LayerList::setCount(int count) {
    ANIM_CHECK_MSG(count >= 0, "layer count must be non-negative");

    if (count > this->count()) {
        while (this->count() < count) add();
        return;
    }

    while (this->count() > count) {
        HobiLayer* layer = _layers.back().get();
        const int index = layer->index();

        if (_root != nullptr) {
            layer->destroyStates();
            _root->graph().disconnect(_layerMixerNode, index);
        }

        // 销毁层的 MixerNode。
        if (_root != nullptr && layer->playable().valid()) {
            _root->graph().destroy(layer->playable());
        }

        _layers.pop_back();
        if (_root != nullptr) _root->graph().setInputCount(_layerMixerNode, this->count());
    }
}

HobiLayer* LayerList::add() {
    if (_root == nullptr) return nullptr;

    if (this->count() >= _capacity) _capacity = std::max(defaultCapacity, _capacity * 2);

    const int index = count();
    const PlayableHandle mixerNode = _root->graph().create<MixerNode>();

    auto layer = std::make_unique<HobiLayer>(_root, index, mixerNode);
    layer->setParent(_root->rootNode());

    _root->graph().setInputCount(_layerMixerNode, index + 1);
    layer->connectToGraph();
    _root->graph().setInputWeight(_layerMixerNode, index, 1.0f);

    _layers.push_back(std::move(layer));
    return _layers.back().get();
}

HobiLayer* LayerList::getLayer(int index) const {
    if (index < 0 || index >= count()) return nullptr;
    return _layers[static_cast<std::size_t>(index)].get();
}

bool LayerList::isAdditive(int layer) const {
    const HobiLayer* value = getLayer(layer);
    return value != nullptr && value->isAdditive();
}

void LayerList::setAdditive(int layer, bool additive) {
    HobiLayer* value = getLayer(layer);
    if (value != nullptr) value->setIsAdditive(additive);
}

void LayerList::setMask(int layer, const AvatarMask* mask) {
    HobiLayer* value = getLayer(layer);
    if (value != nullptr) value->setMask(mask);
}

void LayerList::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    for (const auto& layer : _layers) {
        if (layer != nullptr) layer->gatherAnimationClips(clips);
    }
}

}  // namespace anim
