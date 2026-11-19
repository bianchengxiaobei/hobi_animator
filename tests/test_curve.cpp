#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "anim/clip/Curve.h"

using namespace anim;

namespace {

// 独立的双精度参考实现，用于校验 float 实现（同公式、不同精度）。
double referenceEvaluate(const std::vector<Keyframe>& keys, double time) {
    if (keys.empty()) return 0.0;
    if (keys.size() == 1) return keys.front().value;

    std::size_t upper = 0;
    while (upper < keys.size() && static_cast<double>(keys[upper].time) <= time) ++upper;
    if (upper == 0) return keys.front().value;
    if (upper >= keys.size()) return keys.back().value;

    const Keyframe& k0 = keys[upper - 1];
    const Keyframe& k1 = keys[upper];
    if (k0.constant) return k0.value;

    const double t0 = k0.time;
    const double t1 = k1.time;
    const double dt = t1 - t0;
    if (!(dt > 0.0)) return k0.value;

    const double u = (time - t0) / dt;
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    const double h10 = u3 - 2.0 * u2 + u;
    const double h01 = -2.0 * u3 + 3.0 * u2;
    const double h11 = u3 - u2;
    return h00 * k0.value + h10 * dt * k0.outSlope + h01 * k1.value +
           h11 * dt * k1.inSlope;
}

Curve makeLinearCurve() {
    Curve curve;
    curve.keys = {
        Keyframe{0.0f, 0.0f, 2.0f, 2.0f},
        Keyframe{2.0f, 4.0f, 2.0f, 2.0f},
    };
    return curve;
}

// 取自 Humanoid-Walk.anim 的 RootT.x 通道（首个 m_FloatCurves 项）。
Curve makeWalkRootTX() {
    Curve curve;
    curve.preWrap = WrapMode::Loop;
    curve.postWrap = WrapMode::Loop;
    curve.keys = {
        Keyframe{0.0f, -0.036590192f, -0.07646428f, -0.07646428f},
        Keyframe{0.041666746f, -0.04013528f, -0.0942935f, -0.0942935f},
        Keyframe{0.75f, -0.008985788f, -0.013556819f, -0.013556819f},
        Keyframe{1.0416666f, -0.036590192f, -0.07646428f, -0.07646428f},
    };
    return curve;
}

}  // namespace

TEST(Curve, EmptyAndSingleKey) {
    Curve empty;
    EXPECT_FLOAT_EQ(empty.evaluate(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(empty.length(), 0.0f);

    Curve single;
    single.keys = {Keyframe{0.5f, 3.0f, 0.0f, 0.0f}};
    EXPECT_FLOAT_EQ(single.evaluate(-10.0f), 3.0f);
    EXPECT_FLOAT_EQ(single.evaluate(0.5f), 3.0f);
    EXPECT_FLOAT_EQ(single.evaluate(10.0f), 3.0f);
}

TEST(Curve, HermiteMatchesLinearWhenTangentsMatchSecant) {
    const Curve curve = makeLinearCurve();
    EXPECT_NEAR(curve.evaluate(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(curve.evaluate(0.5f), 1.0f, 1e-6f);
    EXPECT_NEAR(curve.evaluate(1.0f), 2.0f, 1e-6f);
    EXPECT_NEAR(curve.evaluate(1.5f), 3.0f, 1e-6f);
    EXPECT_NEAR(curve.evaluate(2.0f), 4.0f, 1e-6f);
}

TEST(Curve, ConstantSegmentHoldsLeftValue) {
    Curve curve;
    curve.keys = {
        Keyframe{0.0f, 1.0f, 0.0f, 0.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0, 0, true},
        Keyframe{1.0f, 5.0f, 0.0f, 0.0f},
        Keyframe{2.0f, 9.0f, 0.0f, 0.0f},
    };

    EXPECT_FLOAT_EQ(curve.evaluate(0.25f), 1.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.999f), 1.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 5.0f);
    // constant 是“该关键帧 -> 下一关键帧”的属性；k1 未标记，故 1..2 段是平滑的。
    EXPECT_NEAR(curve.evaluate(1.5f), 7.0f, 1e-5f);
    EXPECT_FLOAT_EQ(curve.evaluate(2.0f), 9.0f);

    curve.keys[1].constant = true;
    EXPECT_FLOAT_EQ(curve.evaluate(1.5f), 5.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(2.0f), 9.0f);
}

TEST(Curve, ClampsOutsideRangeWhenWrapDisabled) {
    const Curve curve = makeLinearCurve();
    EXPECT_FLOAT_EQ(curve.evaluate(-5.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(5.0f), 4.0f);
    EXPECT_FLOAT_EQ(curve.evaluateClamped(-5.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.evaluateClamped(5.0f), 4.0f);
}

TEST(Curve, LoopWrapRepeatsPeriod) {
    Curve curve = makeLinearCurve();
    curve.preWrap = WrapMode::Loop;
    curve.postWrap = WrapMode::Loop;

    EXPECT_NEAR(curve.evaluate(-0.5f), curve.evaluate(1.5f), 1e-5f);
    EXPECT_NEAR(curve.evaluate(2.5f), curve.evaluate(0.5f), 1e-5f);
    EXPECT_NEAR(curve.evaluate(4.0f), curve.evaluate(0.0f), 1e-5f);

    // 连续采样绕一圈应当连续（0.25 与 2.25 同相位）。
    EXPECT_NEAR(curve.evaluate(0.25f), curve.evaluate(2.25f), 1e-5f);
}

TEST(Curve, PingPongWrapsMirrored) {
    Curve curve = makeLinearCurve();
    curve.preWrap = WrapMode::PingPong;
    curve.postWrap = WrapMode::PingPong;

    // t=2.5 反射到 1.5，t=3.0 反射到 1.0。
    EXPECT_NEAR(curve.evaluate(2.5f), curve.evaluate(1.5f), 1e-5f);
    EXPECT_NEAR(curve.evaluate(3.0f), curve.evaluate(1.0f), 1e-5f);
}

TEST(Curve, UnclampedExtrapolatesHermite) {
    const Curve curve = makeLinearCurve();
    EXPECT_NEAR(curve.evaluateUnclamped(-0.5f), -1.0f, 1e-5f);
    EXPECT_NEAR(curve.evaluateUnclamped(2.5f), 5.0f, 1e-5f);
}

TEST(Curve, WeightedCollinearWeightsReduceToLinear) {
    // 权重 1/3 + 斜率=割线 => 贝塞尔控制点共线 => 与线性一致。
    Curve curve;
    curve.keys = {
        Keyframe{0.0f, 0.0f, 2.0f, 2.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0, 1},
        Keyframe{2.0f, 4.0f, 2.0f, 2.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0, 1},
    };
    ASSERT_TRUE(curve.isWeighted());
    EXPECT_NEAR(curve.evaluate(0.5f), 1.0f, 1e-5f);
    EXPECT_NEAR(curve.evaluate(1.0f), 2.0f, 1e-5f);
    EXPECT_NEAR(curve.evaluate(1.5f), 3.0f, 1e-5f);
}

TEST(Curve, WeightedEndpointsAndMonotonic) {
    Curve curve;
    curve.keys = {
        Keyframe{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0, 1},
        Keyframe{1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0, 1},
    };

    EXPECT_NEAR(curve.evaluate(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(curve.evaluate(1.0f), 1.0f, 1e-6f);

    float previous = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float value = curve.evaluate(static_cast<float>(i) / 20.0f);
        EXPECT_GE(value, previous - 1e-6f);
        EXPECT_GE(value, -1e-6f);
        EXPECT_LE(value, 1.0f + 1e-6f);
        previous = value;
    }
}

TEST(Curve, MatchesDoublePrecisionReferenceOnRealAnimData) {
    const Curve curve = makeWalkRootTX();

    // 关键帧处必须精确等于存储值。
    for (const Keyframe& key : curve.keys) {
        EXPECT_NEAR(curve.evaluate(key.time), key.value, 1e-6f);
    }

    const float start = curve.keys.front().time;
    const float end = curve.keys.back().time;
    for (int i = 0; i <= 200; ++i) {
        const float t = start + (end - start) * static_cast<float>(i) / 200.0f;
        const float actual = curve.evaluate(t);
        const double expected = referenceEvaluate(curve.keys, static_cast<double>(t));
        EXPECT_NEAR(actual, expected, 1e-5f) << "t=" << t;
    }
}

TEST(Curve, LoopWrapOfRealAnimDataIsContinuous) {
    const Curve curve = makeWalkRootTX();
    const float end = curve.keys.back().time;

    EXPECT_NEAR(curve.evaluate(-0.1f), curve.evaluate(end - 0.1f), 1e-5f);
    EXPECT_NEAR(curve.evaluate(end + 0.25f), curve.evaluate(0.25f), 1e-5f);
}

TEST(Curve, ConstantFlagMakesCurveNonSmoothOnlyAtJump) {
    Curve curve;
    curve.keys = {
        Keyframe{0.0f, 0.0f, 0.0f, 0.0f},
        Keyframe{1.0f, 1.0f, 0.0f, 0.0f},
    };
    // 未标记 constant 时是平滑曲线（中点 0.5）。
    EXPECT_NEAR(curve.evaluate(0.5f), 0.5f, 1e-5f);

    curve.keys[0].constant = true;
    EXPECT_FLOAT_EQ(curve.evaluate(0.5f), 0.0f);
}
