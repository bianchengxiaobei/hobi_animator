#pragma once

#include <initializer_list>
#include <unordered_map>
#include <vector>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/hobi/Key.h"

// 状态字典（对应 HobiPlayable.StateDictionary）。
//
// 文档：docs/07-Hobi门面.md §6
namespace anim {

class HobiPlayable;
class HobiState;
struct Clip;
class ITransition;

class StateDictionary : public IAnimationClipCollection {
public:
    explicit StateDictionary(HobiPlayable& root);

    void registerState(HobiState* state);
    void unregister(HobiState* state);
    bool tryGet(Key key, HobiState*& state) const;
    HobiState* get(Key key) const;
    HobiState* current() const;

    void createIfNew(const std::vector<const Clip*>& clips);
    void createIfNew(std::initializer_list<const Clip*> clips);

    HobiState* getOrCreate(const Clip& clip, bool allowSetClip = false);
    HobiState* getOrCreate(ITransition& transition);

    void destroy(HobiState* state);
    void destroy(const Clip& clip);
    void destroyAll();

    std::vector<HobiState*> states() const;
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

private:
    HobiPlayable* _root = nullptr;
    std::unordered_map<Key, HobiState*, KeyHash> _states;
};

}  // namespace anim
