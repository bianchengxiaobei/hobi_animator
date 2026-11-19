#pragma once

#include <cmath>

#include "anim/animancer/DirectionalAnimationSet.h"

// 八方向动画集合（对应 Utilities/Directional Animations/DirectionalAnimationSet8.cs）。
namespace anim {

class DirectionalAnimationSet8 final : public DirectionalAnimationSet {
public:
    enum class Direction {
        Up,
        Right,
        Down,
        Left,
        UpRight,
        DownRight,
        DownLeft,
        UpLeft,
    };

    struct Diagonals {
        static constexpr float oneOverSqrt2 = 0.70710678118f;

        static Vec2 upRight() { return Vec2(oneOverSqrt2, oneOverSqrt2); }
        static Vec2 downRight() { return Vec2(oneOverSqrt2, -oneOverSqrt2); }
        static Vec2 downLeft() { return Vec2(-oneOverSqrt2, -oneOverSqrt2); }
        static Vec2 upLeft() { return Vec2(-oneOverSqrt2, oneOverSqrt2); }
    };

    DirectionalAnimationSet8() = default;
    ~DirectionalAnimationSet8() override = default;

    using DirectionalAnimationSet::getClip;
    using DirectionalAnimationSet::setClip;

    const Clip* upRight() const noexcept { return _upRight; }
    const Clip* downRight() const noexcept { return _downRight; }
    const Clip* downLeft() const noexcept { return _downLeft; }
    const Clip* upLeft() const noexcept { return _upLeft; }

    void setUpRight(const Clip* clip) {
        assertCanSetClips();
        _upRight = clip;
    }
    void setDownRight(const Clip* clip) {
        assertCanSetClips();
        _downRight = clip;
    }
    void setDownLeft(const Clip* clip) {
        assertCanSetClips();
        _downLeft = clip;
    }
    void setUpLeft(const Clip* clip) {
        assertCanSetClips();
        _upLeft = clip;
    }

    const Clip* getClip(Vec2 direction) const override;
    int clipCount() const noexcept override { return 8; }
    const Clip* getClip(int direction) const override;
    const Clip* getClip(Direction direction) const;

    void setClip(int direction, const Clip* clip) override;
    void setClip(Direction direction, const Clip* clip);

    static Vec2 directionToVector(Direction direction);
    Vec2 getDirection(int direction) const override;
    static Direction vectorToDirection(Vec2 vector);
    static Vec2 snapVectorToDirection(Vec2 vector);
    Vec2 snap(Vec2 vector) const override;

private:
    const Clip* _upRight = nullptr;
    const Clip* _downRight = nullptr;
    const Clip* _downLeft = nullptr;
    const Clip* _upLeft = nullptr;
};

}  // namespace anim
