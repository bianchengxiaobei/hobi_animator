#pragma once

#include <vector>

#include "anim/animancer/AnimationClipCollection.h"
#include "anim/core/Assert.h"
#include "anim/core/Math.h"

// 四方向动画集合（对应 Utilities/Directional Animations/DirectionalAnimationSet.cs）。
// C++ 侧不依赖 ScriptableObject，集合直接持有非拥有的 Clip 指针。
namespace anim {

struct Clip;

class DirectionalAnimationSet : public IAnimationClipCollection {
public:
    enum class Direction {
        Up,
        Right,
        Down,
        Left,
    };

    DirectionalAnimationSet() = default;
    ~DirectionalAnimationSet() override = default;

    const Clip* up() const noexcept { return _up; }
    const Clip* right() const noexcept { return _right; }
    const Clip* down() const noexcept { return _down; }
    const Clip* left() const noexcept { return _left; }

    void setUp(const Clip* clip) {
        assertCanSetClips();
        _up = clip;
    }
    void setRight(const Clip* clip) {
        assertCanSetClips();
        _right = clip;
    }
    void setDown(const Clip* clip) {
        assertCanSetClips();
        _down = clip;
    }
    void setLeft(const Clip* clip) {
        assertCanSetClips();
        _left = clip;
    }

    // 对应 ScriptableObject 的 AllowSetClips/AssertCanSetClips 保护。
    void allowSetClips(bool allow = true) noexcept { _allowSetClips = allow; }
    void assertCanSetClips() const noexcept { ANIM_ASSERT(_allowSetClips); }

    virtual const Clip* getClip(Vec2 direction) const;
    virtual int clipCount() const noexcept { return 4; }
    virtual const Clip* getClip(int direction) const;
    const Clip* getClip(Direction direction) const;

    virtual void setClip(int direction, const Clip* clip);
    void setClip(Direction direction, const Clip* clip);

    static Vec2 directionToVector(Direction direction);
    virtual Vec2 getDirection(int direction) const;
    static Direction vectorToDirection(Vec2 vector);
    static Vec2 snapVectorToDirection(Vec2 vector);
    virtual Vec2 snap(Vec2 vector) const;

    void addClips(std::vector<const Clip*>& clips, int index) const;
    void addDirections(std::vector<Vec2>& directions, int index) const;
    void addClipsAndDirections(std::vector<const Clip*>& clips,
                               std::vector<Vec2>& directions, int index) const;

    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

private:
    const Clip* _up = nullptr;
    const Clip* _right = nullptr;
    const Clip* _down = nullptr;
    const Clip* _left = nullptr;
    bool _allowSetClips = false;
};

}  // namespace anim
