#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "anim/clip/Clip.h"
#include "anim/graph/Nodes.h"
#include "anim/graph/PlayableGraph.h"

using namespace anim;

namespace {

Skeleton makeSkeleton(int boneCount = 1) {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    for (int i = 1; i < boneCount; ++i) {
        skeleton.addBone(hashString("Bone" + std::to_string(i)), i - 1, Transform::identity());
    }
    return skeleton;
}

Pose makePose(const Skeleton& skeleton, int bone, const Vec3& translation,
              const Quat& rotation = kIdentityQuat) {
    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);
    pose[static_cast<std::size_t>(bone)].translation = translation;
    pose[static_cast<std::size_t>(bone)].rotation = rotation;
    return pose;
}

class TestPoseNode final : public PlayableNode {
public:
    const char* typeName() const noexcept override { return "TestPose"; }
    bool hasPose() const noexcept override { return true; }
    void setPose(const Pose& pose) { _output = pose; }
};

BoneLocalClip makeLinearClip(int bone, float from = 0.0f, float to = 10.0f) {
    BoneLocalClip clip;
    clip.length = 1.0f;
    clip.looping = false;

    BoneChannel channel;
    channel.boneIndex = bone;
    channel.hasPosition = 1;
    const float slope = to - from;
    channel.position[0].keys = {
        Keyframe{0.0f, from, slope, slope},
        Keyframe{1.0f, to, slope, slope},
    };

    clip.channels.push_back(channel);
    return clip;
}

struct OffsetJob final : IAnimationJob {
    Vec3 offset{1.0f, 0.0f, 0.0f};
    void processAnimation(Pose& pose, const Skeleton&, float) override {
        pose.local[0].translation += offset;
    }
};

}  // namespace

TEST(PlayableNodes, ClipNodeSamplesAtAdvancedTime) {
    const Skeleton skeleton = makeSkeleton();
    const BoneLocalClip clip = makeLinearClip(0);

    PlayableGraph graph;
    const PlayableHandle node = graph.create<ClipNode>(clip, skeleton);
    graph.setOutput(node);

    graph.evaluate(0.25f);  // time = 0.25
    EXPECT_NEAR(graph.get(node)->outputPose().local[0].translation.x, 2.5f, 1e-5f);

    graph.evaluate(0.25f);  // time = 0.5
    EXPECT_NEAR(graph.get(node)->outputPose().local[0].translation.x, 5.0f, 1e-5f);

    // Non-looping: time is clamped to the duration.
    graph.evaluate(10.0f);
    EXPECT_NEAR(graph.get(node)->outputPose().local[0].translation.x, 10.0f, 1e-5f);
}

TEST(PlayableNodes, ClipNodeLoops) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeLinearClip(0);
    clip.looping = true;

    PlayableGraph graph;
    const PlayableHandle node = graph.create<ClipNode>(clip, skeleton);
    graph.setOutput(node);

    graph.evaluate(1.25f);  // wraps back to 0.25
    EXPECT_NEAR(graph.get(node)->outputPose().local[0].translation.x, 2.5f, 1e-5f);
}

TEST(PlayableNodes, SingleInputNodesPassThroughPoseBuffer) {
    const Skeleton skeleton = makeSkeleton();

    PlayableGraph graph;
    const PlayableHandle source = graph.create<TestPoseNode>();
    const PlayableHandle mixer = graph.create<MixerNode>();
    const PlayableHandle layer = graph.create<LayerMixerNode>();

    static_cast<TestPoseNode*>(graph.get(source))->setPose(
        makePose(skeleton, 0, Vec3(3.0f, 0.0f, 0.0f)));

    graph.connect(source, 0, mixer, 0);
    graph.setInputWeight(mixer, 0, 1.0f);
    graph.connect(mixer, 0, layer, 0);
    graph.setInputWeight(layer, 0, 1.0f);
    graph.setOutput(layer);

    graph.evaluate(0.0f);

    const Pose& sourcePose = graph.get(source)->outputPose();
    EXPECT_EQ(&graph.get(mixer)->outputPose(), &sourcePose);
    EXPECT_EQ(&graph.get(layer)->outputPose(), &sourcePose);
}

TEST(PlayableNodes, MixerBlendsWeightedPoses) {
    const Skeleton skeleton = makeSkeleton();
    const BoneLocalClip clipA = makeLinearClip(0, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(0, 10.0f, 20.0f);

    PlayableGraph graph;
    const PlayableHandle a = graph.create<ClipNode>(clipA, skeleton);
    const PlayableHandle b = graph.create<ClipNode>(clipB, skeleton);
    const PlayableHandle mixer = graph.create<MixerNode>();

    graph.connect(a, 0, mixer, 0);
    graph.connect(b, 0, mixer, 1);
    graph.setInputWeight(mixer, 0, 0.5f);
    graph.setInputWeight(mixer, 1, 0.5f);
    graph.setOutput(mixer);

    graph.evaluate(0.25f);
    // A = 2.5, B = 12.5 -> 7.5
    EXPECT_NEAR(graph.get(mixer)->outputPose().local[0].translation.x, 7.5f, 1e-5f);
}

TEST(PlayableNodes, LayerMixerOverridesWithWeight) {
    const Skeleton skeleton = makeSkeleton();

    PlayableGraph graph;
    const PlayableHandle base = graph.create<TestPoseNode>();
    const PlayableHandle top = graph.create<TestPoseNode>();
    const PlayableHandle mixer = graph.create<LayerMixerNode>();

    static_cast<TestPoseNode*>(graph.get(base))->setPose(makePose(skeleton, 0, Vec3(0.0f)));
    static_cast<TestPoseNode*>(graph.get(top))->setPose(
        makePose(skeleton, 0, Vec3(10.0f, 0.0f, 0.0f),
                 glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f))));

    graph.connect(base, 0, mixer, 0);
    graph.connect(top, 0, mixer, 1);
    graph.setInputWeight(mixer, 0, 1.0f);
    graph.setInputWeight(mixer, 1, 0.5f);
    graph.setOutput(mixer);

    graph.evaluate(0.0f);

    const Transform& result = graph.get(mixer)->outputPose().local[0];
    EXPECT_NEAR(result.translation.x, 5.0f, 1e-5f);
    EXPECT_NEAR(glm::angle(result.rotation), radians(45.0f), 1e-5f);
}

TEST(PlayableNodes, LayerMixerAdditiveAddsDelta) {
    const Skeleton skeleton = makeSkeleton();

    PlayableGraph graph;
    const PlayableHandle base = graph.create<TestPoseNode>();
    const PlayableHandle top = graph.create<TestPoseNode>();
    const PlayableHandle mixer = graph.create<LayerMixerNode>();

    static_cast<TestPoseNode*>(graph.get(base))->setPose(makePose(skeleton, 0, Vec3(0.0f)));
    static_cast<TestPoseNode*>(graph.get(top))->setPose(
        makePose(skeleton, 0, Vec3(2.0f, 0.0f, 0.0f),
                 glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f))));

    graph.connect(base, 0, mixer, 0);
    graph.connect(top, 0, mixer, 1);
    graph.setInputWeight(mixer, 1, 0.5f);
    static_cast<LayerMixerNode*>(graph.get(mixer))->setLayerAdditive(1, true);
    graph.setOutput(mixer);

    graph.evaluate(0.0f);

    const Transform& result = graph.get(mixer)->outputPose().local[0];
    EXPECT_NEAR(result.translation.x, 1.0f, 1e-5f);
    EXPECT_NEAR(glm::angle(result.rotation), radians(45.0f), 1e-5f);
}

TEST(PlayableNodes, LayerMixerHonoursAvatarMask) {
    const Skeleton skeleton = makeSkeleton(2);

    PlayableGraph graph;
    const PlayableHandle base = graph.create<TestPoseNode>();
    const PlayableHandle top = graph.create<TestPoseNode>();
    const PlayableHandle mixer = graph.create<LayerMixerNode>();

    static_cast<TestPoseNode*>(graph.get(base))->setPose(makePose(skeleton, 0, Vec3(0.0f)));
    static_cast<TestPoseNode*>(graph.get(top))->setPose(
        makePose(skeleton, 0, Vec3(10.0f, 10.0f, 0.0f)));

    AvatarMask mask;
    mask.boneWeights = {1.0f, 0.0f};

    graph.connect(base, 0, mixer, 0);
    graph.connect(top, 0, mixer, 1);
    graph.setInputWeight(mixer, 1, 1.0f);
    static_cast<LayerMixerNode*>(graph.get(mixer))->setLayerMask(1, &mask);
    graph.setOutput(mixer);

    graph.evaluate(0.0f);

    const Pose& result = graph.get(mixer)->outputPose();
    EXPECT_NEAR(result.local[0].translation.x, 10.0f, 1e-5f);  // masked in
    EXPECT_NEAR(result.local[1].translation.x, 0.0f, 1e-5f);   // masked out
}

TEST(PlayableNodes, ScriptNodePassesThroughInputPose) {
    const Skeleton skeleton = makeSkeleton();

    PlayableGraph graph;
    const PlayableHandle source = graph.create<TestPoseNode>();
    const PlayableHandle script = graph.create<ScriptNode>();

    static_cast<TestPoseNode*>(graph.get(source))->setPose(
        makePose(skeleton, 0, Vec3(3.0f, 0.0f, 0.0f)));

    graph.connect(source, 0, script, 0);
    graph.setOutput(script);

    graph.evaluate(0.0f);

    EXPECT_TRUE(graph.get(script)->hasPose());
    EXPECT_NEAR(graph.get(script)->outputPose().local[0].translation.x, 3.0f, 1e-5f);
}

TEST(PlayableNodes, ScriptPoseNodeRunsJobOnInputPose) {
    const Skeleton skeleton = makeSkeleton();

    PlayableGraph graph;
    const PlayableHandle source = graph.create<TestPoseNode>();
    const PlayableHandle script = graph.create<ScriptPoseNode>(skeleton);

    static_cast<TestPoseNode*>(graph.get(source))->setPose(
        makePose(skeleton, 0, Vec3(2.0f, 0.0f, 0.0f)));

    OffsetJob job;
    static_cast<ScriptPoseNode*>(graph.get(script))->job = &job;

    graph.connect(source, 0, script, 0);
    graph.setOutput(script);

    graph.evaluate(0.0f);

    EXPECT_NEAR(graph.get(script)->outputPose().local[0].translation.x, 3.0f, 1e-5f);
}
