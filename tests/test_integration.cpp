#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include "anim/integration/Coordinate.h"
#include "anim/integration/Skinning.h"
#include "anim/jobs/ThreadPoolJobScheduler.h"

using namespace anim;

TEST(Integration, ThreadPoolRespectsDependencies) {
    ThreadPoolJobScheduler scheduler(4);
    EXPECT_EQ(scheduler.workerCount(), 4);

    std::atomic<int> value{0};
    std::atomic<int> observed{0};

    const JobHandle first = scheduler.schedule([&] { value.store(1); });
    const std::array<JobHandle, 1> deps{first};
    const JobHandle second = scheduler.schedule(
        [&] {
            observed.store(value.load());
            value.store(2);
        },
        deps);

    scheduler.complete(second);
    scheduler.waitAll();

    EXPECT_EQ(observed.load(), 1);
    EXPECT_EQ(value.load(), 2);
}

TEST(Integration, ThreadPoolParallelForCoversRange) {
    ThreadPoolJobScheduler scheduler(4);
    std::atomic<long long> sum{0};

    scheduler.parallelFor(1000, [&](std::size_t begin, std::size_t end) {
        long long local = 0;
        for (std::size_t i = begin; i < end; ++i) local += static_cast<long long>(i);
        sum.fetch_add(local);
    });

    EXPECT_EQ(sum.load(), 1000LL * 999LL / 2LL);
}

TEST(Integration, ThreadPoolWaitAllAfterMultipleTasks) {
    ThreadPoolJobScheduler scheduler(3);
    std::atomic<int> counter{0};

    for (int i = 0; i < 100; ++i) {
        scheduler.schedule([&] { counter.fetch_add(1); });
    }
    scheduler.waitAll();

    EXPECT_EQ(counter.load(), 100);
}

TEST(Integration, FlipZIsInvolution) {
    Transform transform;
    transform.translation = Vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = glm::angleAxis(radians(35.0f), glm::normalize(Vec3(1.0f, 2.0f, -1.0f)));
    transform.scale = Vec3(1.0f, 1.5f, 0.5f);

    const Transform flipped = flipZ(flipZ(transform));
    EXPECT_NEAR(flipped.translation.x, transform.translation.x, 1e-6f);
    EXPECT_NEAR(flipped.translation.y, transform.translation.y, 1e-6f);
    EXPECT_NEAR(flipped.translation.z, transform.translation.z, 1e-6f);
    EXPECT_NEAR(std::abs(glm::dot(flipped.rotation, transform.rotation)), 1.0f, 1e-5f);

    const Mat4 matrix = transform.toMatrix();
    const Mat4 roundTrip = flipZ(flipZ(matrix));
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(roundTrip[column][row], matrix[column][row], 1e-5f);
        }
    }
}

TEST(Integration, FlipZPreservesWorldChain) {
    const Mat4 parent = Transform{Vec3(0.0f, 1.0f, 2.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                  Vec3(1.0f)}.toMatrix();
    const Mat4 child = Transform{Vec3(0.0f, 0.5f, -0.25f),
                                 glm::angleAxis(radians(20.0f), Vec3(1.0f, 0.0f, 0.0f)),
                                 Vec3(1.0f)}.toMatrix();

    const Mat4 expected = flipZ(parent * child);
    const Mat4 actual = flipZ(parent) * flipZ(child);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(actual[column][row], expected[column][row], 1e-5f);
        }
    }
}

TEST(Integration, FlipZPreservesRotatedWorldChain) {
    const Transform parentTransform{
        Vec3(1.0f, 2.0f, 3.0f),
        glm::angleAxis(radians(37.0f), glm::normalize(Vec3(1.0f, 2.0f, -1.0f))),
        Vec3(1.0f, 1.5f, 0.5f)};
    const Transform childTransform{
        Vec3(0.25f, -0.5f, 0.75f),
        glm::angleAxis(radians(-51.0f), glm::normalize(Vec3(-2.0f, 1.0f, 3.0f))),
        Vec3(0.75f, 1.0f, 1.25f)};

    const Mat4 expected =
        flipZ(parentTransform.toMatrix() * childTransform.toMatrix());
    const Mat4 actual =
        flipZ(parentTransform).toMatrix() * flipZ(childTransform).toMatrix();
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(actual[column][row], expected[column][row], 1e-5f);
        }
    }

    const Quat expectedRotation = flipZ(parentTransform.rotation * childTransform.rotation);
    const Quat actualRotation = flipZ(parentTransform.rotation) * flipZ(childTransform.rotation);
    EXPECT_NEAR(std::abs(glm::dot(actualRotation, expectedRotation)), 1.0f, 1e-5f);
}

TEST(Integration, TransposePaletteForRowMajorUpload) {
    Mat4 matrix(1.0f);
    matrix[0][1] = 2.0f;
    matrix[1][0] = 3.0f;
    matrix[2][3] = 4.0f;

    const std::vector<Mat4> source{matrix};
    std::vector<Mat4> destination;
    transposePalette(source, destination);

    ASSERT_EQ(destination.size(), 1u);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_FLOAT_EQ(destination[0][column][row], source[0][row][column]);
        }
    }
}
