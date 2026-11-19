#include "anim/animancer/DirectionalAnimationSet.h"

#include <cmath>

#include "anim/animancer/AnimationClipCollection.h"
#include "anim/animancer/DirectionalAnimationSet8.h"
#include "anim/clip/Clip.h"
#include "anim/core/Assert.h"

namespace anim {

const Clip* DirectionalAnimationSet::getClip(Vec2 direction) const {
    if (direction.x >= 0.0f) {
        if (direction.y >= 0.0f)
            return direction.x > direction.y ? _right : _up;
        return direction.x > -direction.y ? _right : _down;
    }

    if (direction.y >= 0.0f)
        return direction.x < -direction.y ? _left : _up;
    return direction.x < direction.y ? _left : _down;
}

const Clip* DirectionalAnimationSet::getClip(int direction) const {
    return getClip(static_cast<Direction>(direction));
}

const Clip* DirectionalAnimationSet::getClip(Direction direction) const {
    switch (direction) {
        case Direction::Up: return _up;
        case Direction::Right: return _right;
        case Direction::Down: return _down;
        case Direction::Left: return _left;
    }

    ANIM_CHECK_MSG(false, "Unsupported DirectionalAnimationSet direction");
    return nullptr;
}

void DirectionalAnimationSet::setClip(int direction, const Clip* clip) {
    setClip(static_cast<Direction>(direction), clip);
}

void DirectionalAnimationSet::setClip(Direction direction, const Clip* clip) {
    switch (direction) {
        case Direction::Up: setUp(clip); return;
        case Direction::Right: setRight(clip); return;
        case Direction::Down: setDown(clip); return;
        case Direction::Left: setLeft(clip); return;
    }

    ANIM_CHECK_MSG(false, "Unsupported DirectionalAnimationSet direction");
}

Vec2 DirectionalAnimationSet::directionToVector(Direction direction) {
    switch (direction) {
        case Direction::Up: return Vec2(0.0f, 1.0f);
        case Direction::Right: return Vec2(1.0f, 0.0f);
        case Direction::Down: return Vec2(0.0f, -1.0f);
        case Direction::Left: return Vec2(-1.0f, 0.0f);
    }

    ANIM_CHECK_MSG(false, "Unsupported DirectionalAnimationSet direction");
    return Vec2(0.0f);
}

Vec2 DirectionalAnimationSet::getDirection(int direction) const {
    return directionToVector(static_cast<Direction>(direction));
}

DirectionalAnimationSet::Direction DirectionalAnimationSet::vectorToDirection(Vec2 vector) {
    if (vector.x >= 0.0f) {
        if (vector.y >= 0.0f)
            return vector.x > vector.y ? Direction::Right : Direction::Up;
        return vector.x > -vector.y ? Direction::Right : Direction::Down;
    }

    if (vector.y >= 0.0f)
        return vector.x < -vector.y ? Direction::Left : Direction::Up;
    return vector.x < vector.y ? Direction::Left : Direction::Down;
}

Vec2 DirectionalAnimationSet::snapVectorToDirection(Vec2 vector) {
    const float magnitude = glm::length(vector);
    return directionToVector(vectorToDirection(vector)) * magnitude;
}

Vec2 DirectionalAnimationSet::snap(Vec2 vector) const {
    return snapVectorToDirection(vector);
}

void DirectionalAnimationSet::addClips(std::vector<const Clip*>& clips, int index) const {
    ANIM_CHECK_MSG(index >= 0, "index must be non-negative");
    const int count = clipCount();
    if (static_cast<int>(clips.size()) < index + count)
        clips.resize(static_cast<std::size_t>(index + count));
    for (int i = 0; i < count; ++i) clips[static_cast<std::size_t>(index + i)] = getClip(i);
}

void DirectionalAnimationSet::addDirections(std::vector<Vec2>& directions, int index) const {
    ANIM_CHECK_MSG(index >= 0, "index must be non-negative");
    const int count = clipCount();
    if (static_cast<int>(directions.size()) < index + count)
        directions.resize(static_cast<std::size_t>(index + count));
    for (int i = 0; i < count; ++i)
        directions[static_cast<std::size_t>(index + i)] = getDirection(i);
}

void DirectionalAnimationSet::addClipsAndDirections(std::vector<const Clip*>& clips,
                                                    std::vector<Vec2>& directions,
                                                    int index) const {
    addClips(clips, index);
    addDirections(directions, index);
}

void DirectionalAnimationSet::gatherAnimationClips(
    std::vector<const Clip*>& clips) const {
    const int count = clipCount();
    for (int i = 0; i < count; ++i) gatherClip(clips, getClip(i));
}

// ---------------------------------------------------------------------------
// DirectionalAnimationSet8
// ---------------------------------------------------------------------------

namespace {

int octantOf(Vec2 direction) {
    const float angle = std::atan2(direction.y, direction.x);
    const int octant =
        static_cast<int>(std::nearbyint(8.0f * angle / (2.0f * kPi) + 8.0f)) % 8;
    return octant;
}

}  // namespace

const Clip* DirectionalAnimationSet8::getClip(Vec2 direction) const {
    return getClip(vectorToDirection(direction));
}

const Clip* DirectionalAnimationSet8::getClip(int direction) const {
    return getClip(static_cast<Direction>(direction));
}

const Clip* DirectionalAnimationSet8::getClip(Direction direction) const {
    switch (direction) {
        case Direction::Up: return up();
        case Direction::Right: return right();
        case Direction::Down: return down();
        case Direction::Left: return left();
        case Direction::UpRight: return _upRight;
        case Direction::DownRight: return _downRight;
        case Direction::DownLeft: return _downLeft;
        case Direction::UpLeft: return _upLeft;
    }

    ANIM_CHECK_MSG(false, "Unsupported DirectionalAnimationSet8 direction");
    return nullptr;
}

void DirectionalAnimationSet8::setClip(int direction, const Clip* clip) {
    setClip(static_cast<Direction>(direction), clip);
}

void DirectionalAnimationSet8::setClip(Direction direction, const Clip* clip) {
    switch (direction) {
        case Direction::Up: setUp(clip); return;
        case Direction::Right: setRight(clip); return;
        case Direction::Down: setDown(clip); return;
        case Direction::Left: setLeft(clip); return;
        case Direction::UpRight: setUpRight(clip); return;
        case Direction::DownRight: setDownRight(clip); return;
        case Direction::DownLeft: setDownLeft(clip); return;
        case Direction::UpLeft: setUpLeft(clip); return;
    }

    ANIM_CHECK_MSG(false, "Unsupported DirectionalAnimationSet8 direction");
}

Vec2 DirectionalAnimationSet8::directionToVector(Direction direction) {
    switch (direction) {
        case Direction::Up: return Vec2(0.0f, 1.0f);
        case Direction::Right: return Vec2(1.0f, 0.0f);
        case Direction::Down: return Vec2(0.0f, -1.0f);
        case Direction::Left: return Vec2(-1.0f, 0.0f);
        case Direction::UpRight: return Diagonals::upRight();
        case Direction::DownRight: return Diagonals::downRight();
        case Direction::DownLeft: return Diagonals::downLeft();
        case Direction::UpLeft: return Diagonals::upLeft();
    }

    ANIM_CHECK_MSG(false, "Unsupported DirectionalAnimationSet8 direction");
    return Vec2(0.0f);
}

Vec2 DirectionalAnimationSet8::getDirection(int direction) const {
    return directionToVector(static_cast<Direction>(direction));
}

DirectionalAnimationSet8::Direction DirectionalAnimationSet8::vectorToDirection(Vec2 vector) {
    switch (octantOf(vector)) {
        case 0: return Direction::Right;
        case 1: return Direction::UpRight;
        case 2: return Direction::Up;
        case 3: return Direction::UpLeft;
        case 4: return Direction::Left;
        case 5: return Direction::DownLeft;
        case 6: return Direction::Down;
        case 7: return Direction::DownRight;
        default:
            ANIM_CHECK_MSG(false, "Invalid octant");
            return Direction::Right;
    }
}

Vec2 DirectionalAnimationSet8::snapVectorToDirection(Vec2 vector) {
    const float magnitude = glm::length(vector);
    return directionToVector(vectorToDirection(vector)) * magnitude;
}

Vec2 DirectionalAnimationSet8::snap(Vec2 vector) const {
    return snapVectorToDirection(vector);
}

}  // namespace anim
