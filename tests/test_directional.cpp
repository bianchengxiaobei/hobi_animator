#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "anim/animancer/DirectionalAnimationSet.h"
#include "anim/animancer/DirectionalAnimationSet8.h"
#include "anim/animancer/DirectionalClipTransition.h"
#include "anim/clip/Clip.h"

using namespace anim;

namespace {

BoneLocalClip makeClip(float length) {
    BoneLocalClip clip;
    clip.length = length;
    return clip;
}

}  // namespace

TEST(DirectionalAnimationSet, SelectsCardinalClipsAndSnaps) {
    BoneLocalClip up = makeClip(1.0f);
    BoneLocalClip right = makeClip(1.0f);
    BoneLocalClip down = makeClip(1.0f);
    BoneLocalClip left = makeClip(1.0f);

    DirectionalAnimationSet set;
    set.allowSetClips();
    set.setUp(&up);
    set.setRight(&right);
    set.setDown(&down);
    set.setLeft(&left);

    EXPECT_EQ(set.clipCount(), 4);
    EXPECT_EQ(set.getClip(Vec2(0.0f, 1.0f)), &up);
    EXPECT_EQ(set.getClip(Vec2(1.0f, 0.0f)), &right);
    EXPECT_EQ(set.getClip(Vec2(0.0f, -1.0f)), &down);
    EXPECT_EQ(set.getClip(Vec2(-1.0f, 0.0f)), &left);
    EXPECT_EQ(set.getClip(static_cast<int>(DirectionalAnimationSet::Direction::Right)), &right);
    EXPECT_EQ(set.getDirection(0), Vec2(0.0f, 1.0f));

    const Vec2 snapped = set.snap(Vec2(0.0f, 5.0f));
    EXPECT_FLOAT_EQ(snapped.x, 0.0f);
    EXPECT_FLOAT_EQ(snapped.y, 5.0f);

    std::vector<const Clip*> clips;
    set.gatherAnimationClips(clips);
    EXPECT_EQ(clips.size(), 4u);
}

TEST(DirectionalAnimationSet8, SelectsDiagonalsAndOctants) {
    BoneLocalClip up = makeClip(1.0f);
    BoneLocalClip right = makeClip(1.0f);
    BoneLocalClip down = makeClip(1.0f);
    BoneLocalClip left = makeClip(1.0f);
    BoneLocalClip upRight = makeClip(1.0f);
    BoneLocalClip downRight = makeClip(1.0f);
    BoneLocalClip downLeft = makeClip(1.0f);
    BoneLocalClip upLeft = makeClip(1.0f);

    DirectionalAnimationSet8 set;
    set.allowSetClips();
    set.setClip(DirectionalAnimationSet8::Direction::Up, &up);
    set.setClip(DirectionalAnimationSet8::Direction::Right, &right);
    set.setClip(DirectionalAnimationSet8::Direction::Down, &down);
    set.setClip(DirectionalAnimationSet8::Direction::Left, &left);
    set.setClip(DirectionalAnimationSet8::Direction::UpRight, &upRight);
    set.setClip(DirectionalAnimationSet8::Direction::DownRight, &downRight);
    set.setClip(DirectionalAnimationSet8::Direction::DownLeft, &downLeft);
    set.setClip(DirectionalAnimationSet8::Direction::UpLeft, &upLeft);

    EXPECT_EQ(set.clipCount(), 8);
    EXPECT_EQ(set.getClip(Vec2(1.0f, 1.0f)), &upRight);
    EXPECT_EQ(set.getClip(Vec2(1.0f, -1.0f)), &downRight);
    EXPECT_EQ(set.getClip(Vec2(-1.0f, -1.0f)), &downLeft);
    EXPECT_EQ(set.getClip(Vec2(-1.0f, 1.0f)), &upLeft);

    EXPECT_EQ(DirectionalAnimationSet8::vectorToDirection(Vec2(1.0f, 0.2f)),
              DirectionalAnimationSet8::Direction::Right);
    EXPECT_NEAR(set.getDirection(4).x, 0.70710678f, 1e-6f);
    EXPECT_NEAR(set.getDirection(4).y, 0.70710678f, 1e-6f);

    const Vec2 snapped = set.snap(Vec2(3.0f, 3.0f));
    EXPECT_NEAR(snapped.x, 3.0f, 1e-5f);
    EXPECT_NEAR(snapped.y, 3.0f, 1e-5f);

    std::vector<const Clip*> clips;
    set.gatherAnimationClips(clips);
    EXPECT_EQ(clips.size(), 8u);
}

TEST(DirectionalClipTransition, SetsClipAndGathersWholeSet) {
    BoneLocalClip up = makeClip(1.0f);
    BoneLocalClip right = makeClip(1.0f);
    BoneLocalClip down = makeClip(1.0f);
    BoneLocalClip left = makeClip(1.0f);
    BoneLocalClip upRight = makeClip(1.0f);
    BoneLocalClip downRight = makeClip(1.0f);
    BoneLocalClip downLeft = makeClip(1.0f);
    BoneLocalClip upLeft = makeClip(1.0f);

    DirectionalAnimationSet8 set;
    set.allowSetClips();
    set.setClip(DirectionalAnimationSet8::Direction::Up, &up);
    set.setClip(DirectionalAnimationSet8::Direction::Right, &right);
    set.setClip(DirectionalAnimationSet8::Direction::Down, &down);
    set.setClip(DirectionalAnimationSet8::Direction::Left, &left);
    set.setClip(DirectionalAnimationSet8::Direction::UpRight, &upRight);
    set.setClip(DirectionalAnimationSet8::Direction::DownRight, &downRight);
    set.setClip(DirectionalAnimationSet8::Direction::DownLeft, &downLeft);
    set.setClip(DirectionalAnimationSet8::Direction::UpLeft, &upLeft);

    DirectionalClipTransition transition;
    transition.animationSet = &set;
    transition.setDirection(DirectionalAnimationSet8::Direction::DownLeft);
    EXPECT_EQ(transition.clip, &downLeft);

    transition.setDirection(Vec2(1.0f, 1.0f));
    EXPECT_EQ(transition.clip, &upRight);

    std::vector<const Clip*> clips;
    transition.gatherAnimationClips(clips);
    EXPECT_EQ(clips.size(), 8u);
}
