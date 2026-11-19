#include <gtest/gtest.h>

#include "Input.h"

using demo::InputState;
using demo::Key;

TEST(BgfxInput, MapsPlaybackAndPreviewKeys) {
    InputState input;

    input.onKeyDown(Key::Space);
    input.onKeyDown(Key::Period);
    input.onKeyDown(Key::R);
    input.onKeyDown(Key::D);
    input.onKeyDown(Key::E);
    input.onKeyDown(Key::G);
    input.onKeyDown(Key::S);
    input.onKeyDown(Key::F);
    input.onKeyDown(Key::M);
    input.onKeyDown(Key::T);
    input.onKeyDown(Key::L);
    input.onKeyDown(Key::K);
    input.onKeyDown(Key::V);

    EXPECT_TRUE(input.togglePlay);
    EXPECT_TRUE(input.stepRequested);
    EXPECT_TRUE(input.restartRequested);
    EXPECT_TRUE(input.toggleOverlay);
    EXPECT_TRUE(input.toggleEvents);
    EXPECT_TRUE(input.dumpGraph);
    EXPECT_TRUE(input.saveSnapshot);
    EXPECT_TRUE(input.cyclePreviewFade);
    EXPECT_TRUE(input.cyclePreviewFadeMode);
    EXPECT_TRUE(input.cyclePreviewStart);
    EXPECT_TRUE(input.togglePreviewLoop);
    EXPECT_TRUE(input.resetPreview);
    EXPECT_TRUE(input.toggleSkeleton);
    EXPECT_EQ(input.clipDelta, 0);
    EXPECT_EQ(input.layerDelta, 0);
    EXPECT_EQ(input.speedDelta, 0);
    EXPECT_EQ(input.previewSpeedDelta, 0);
}

TEST(BgfxInput, AccumulatesDeltaKeys) {
    InputState input;

    input.onKeyDown(Key::Up);
    input.onKeyDown(Key::Up);
    input.onKeyDown(Key::Down);
    input.onKeyDown(Key::Tab);
    input.onKeyDown(Key::Tab);
    input.onKeyDown(Key::LeftBracket);
    input.onKeyDown(Key::RightBracket);
    input.onKeyDown(Key::RightBracket);
    input.onKeyDown(Key::Minus);
    input.onKeyDown(Key::Equals);
    input.onKeyDown(Key::Equals);

    EXPECT_EQ(input.clipDelta, 1);
    EXPECT_EQ(input.layerDelta, 2);
    EXPECT_EQ(input.speedDelta, 1);
    EXPECT_EQ(input.previewSpeedDelta, 1);
}
