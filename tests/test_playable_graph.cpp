#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "anim/graph/Nodes.h"
#include "anim/graph/PlayableGraph.h"

using namespace anim;

namespace {

ScriptNode* asScript(PlayableGraph& graph, PlayableHandle handle) {
    return static_cast<ScriptNode*>(graph.get(handle));
}

}  // namespace

TEST(PlayableGraph, CreateDestroyAndSlotReuse) {
    PlayableGraph graph;
    const PlayableHandle a = graph.create<ScriptNode>();
    EXPECT_TRUE(a.valid());
    EXPECT_NE(graph.get(a), nullptr);
    EXPECT_EQ(graph.liveNodeCount(), 1);

    const uint32_t index = a.index;
    graph.destroy(a);
    EXPECT_EQ(graph.get(a), nullptr);
    EXPECT_EQ(graph.liveNodeCount(), 0);

    const PlayableHandle b = graph.create<ScriptNode>();
    EXPECT_EQ(b.index, index);          // 槽位复用
    EXPECT_NE(b.version, a.version);    // 版本自增
    EXPECT_NE(graph.get(b), nullptr);
}

TEST(PlayableGraph, DestroyDisconnectsParentsAndChildren) {
    PlayableGraph graph;
    const PlayableHandle child = graph.create<ScriptNode>();
    const PlayableHandle parent = graph.create<ScriptNode>();

    graph.connect(child, 0, parent, 0);
    EXPECT_EQ(graph.get(parent)->input(0), child);

    graph.destroy(child);
    EXPECT_FALSE(graph.get(parent)->input(0).valid());
    EXPECT_EQ(graph.get(child), nullptr);

    // 父节点仍然存活。
    EXPECT_NE(graph.get(parent), nullptr);
}

TEST(PlayableGraph, DestroySubgraphRemovesUnreferencedChildren) {
    PlayableGraph graph;
    const PlayableHandle a = graph.create<ScriptNode>();
    const PlayableHandle b = graph.create<ScriptNode>();
    const PlayableHandle c = graph.create<ScriptNode>();

    graph.connect(b, 0, a, 0);
    graph.connect(c, 0, b, 0);
    graph.setOutput(a);

    graph.destroy(a);
    EXPECT_EQ(graph.get(a), nullptr);
    EXPECT_EQ(graph.get(b), nullptr);
    EXPECT_EQ(graph.get(c), nullptr);
    EXPECT_EQ(graph.liveNodeCount(), 0);
}

TEST(PlayableGraph, CompileDetectsCycle) {
    PlayableGraph graph;
    const PlayableHandle a = graph.create<MixerNode>();
    const PlayableHandle b = graph.create<MixerNode>();

    graph.connect(a, 0, b, 0);
    graph.connect(b, 0, a, 0);
    graph.setOutput(a);

    EXPECT_FALSE(graph.compile());
    EXPECT_NE(graph.lastError().find("cycle"), std::string::npos);
}

TEST(PlayableGraph, CompileWithoutOutputSucceeds) {
    PlayableGraph graph;
    graph.create<ScriptNode>();
    EXPECT_TRUE(graph.compile());
    EXPECT_EQ(graph.diagnostics().reachableNodes, 0u);
}

TEST(PlayableGraph, PrepareRunsParentToChildAndProcessChildToParent) {
    PlayableGraph graph;

    std::vector<std::string> log;

    const PlayableHandle root = graph.create<ScriptNode>();
    const PlayableHandle middle = graph.create<ScriptNode>();
    const PlayableHandle leaf = graph.create<ScriptNode>();

    auto hook = [&log](const char* name) {
        return [&log, name](PlayableNode&, float) { log.push_back(name); };
    };

    asScript(graph, root)->setPrepareCallback(hook("root.prepare"));
    asScript(graph, root)->setProcessCallback(hook("root.process"));
    asScript(graph, middle)->setPrepareCallback(hook("middle.prepare"));
    asScript(graph, middle)->setProcessCallback(hook("middle.process"));
    asScript(graph, leaf)->setPrepareCallback(hook("leaf.prepare"));
    asScript(graph, leaf)->setProcessCallback(hook("leaf.process"));

    graph.connect(middle, 0, root, 0);
    graph.connect(leaf, 0, middle, 0);
    graph.setOutput(root);

    graph.evaluate(0.016f);

    ASSERT_EQ(log.size(), 6u);
    EXPECT_EQ(log[0], "root.prepare");
    EXPECT_EQ(log[1], "middle.prepare");
    EXPECT_EQ(log[2], "leaf.prepare");
    EXPECT_EQ(log[3], "leaf.process");
    EXPECT_EQ(log[4], "middle.process");
    EXPECT_EQ(log[5], "root.process");
}

TEST(PlayableGraph, PostUpdateStyleTwoInputNodeIsPreparedLast) {
    PlayableGraph graph;

    std::vector<std::string> log;
    const PlayableHandle root = graph.create<ScriptNode>();
    const PlayableHandle main = graph.create<ScriptNode>();
    const PlayableHandle post = graph.create<ScriptNode>();

    graph.setInputCount(root, 2);
    graph.connect(main, 0, root, 0);
    graph.connect(post, 0, root, 1);
    graph.setOutput(root);

    asScript(graph, main)->setPrepareCallback(
        [&log](PlayableNode&, float) { log.push_back("main"); });
    asScript(graph, post)->setPrepareCallback(
        [&log](PlayableNode&, float) { log.push_back("post"); });

    graph.evaluate(0.016f);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "main");
    EXPECT_EQ(log[1], "post");
}

TEST(PlayableGraph, StopPausesEvaluation) {
    PlayableGraph graph;
    const PlayableHandle node = graph.create<ScriptNode>();
    graph.setOutput(node);

    int calls = 0;
    asScript(graph, node)->setProcessCallback(
        [&calls](PlayableNode&, float) { ++calls; });

    graph.evaluate(0.016f);
    EXPECT_EQ(calls, 1);

    graph.stop();
    graph.evaluate(0.016f);
    EXPECT_EQ(calls, 1);
}

TEST(PlayableGraph, RejectsNegativeInputWeight) {
    PlayableGraph graph;
    const PlayableHandle node = graph.create<MixerNode>();
    EXPECT_THROW(graph.setInputWeight(node, 0, -1.0f), std::logic_error);
}

TEST(PlayableGraph, ConnectRejectsInvalidHandles) {
    PlayableGraph graph;
    const PlayableHandle node = graph.create<MixerNode>();
    EXPECT_THROW(graph.connect(node, 0, PlayableHandle{}, 0), std::logic_error);
}
