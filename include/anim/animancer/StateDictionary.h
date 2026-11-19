#pragma once

#include <initializer_list>
#include <unordered_map>
#include <vector>

#include "anim/animancer/AnimationClipCollection.h"
#include "anim/animancer/Key.h"

// 状态字典（对应 AnimancerPlayable.StateDictionary）。
//
// 文档：docs/07-Animancer门面.md §6
namespace anim {

class AnimancerPlayable;
class AnimancerState;
struct Clip;
class ITransition;

class StateDictionary : public IAnimationClipCollection {
public:
    explicit StateDictionary(AnimancerPlayable& root);

    void registerState(AnimancerState* state);
    void unregister(AnimancerState* state);
    bool tryGet(Key key, AnimancerState*& state) const;
    AnimancerState* get(Key key) const;
    AnimancerState* current() const;

    void createIfNew(const std::vector<const Clip*>& clips);
    void createIfNew(std::initializer_list<const Clip*> clips);

    AnimancerState* getOrCreate(const Clip& clip, bool allowSetClip = false);
    AnimancerState* getOrCreate(ITransition& transition);

    void destroy(AnimancerState* state);
    void destroy(const Clip& clip);
    void destroyAll();

    std::vector<AnimancerState*> states() const;
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

private:
    AnimancerPlayable* _root = nullptr;
    std::unordered_map<Key, AnimancerState*, KeyHash> _states;
};

}  // namespace anim
