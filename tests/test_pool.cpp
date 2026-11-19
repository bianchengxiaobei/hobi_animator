#include <gtest/gtest.h>

#include <stdexcept>

#include "anim/core/Pool.h"

using namespace anim;

namespace {
struct Item {
    int value = 0;
};
}  // namespace

TEST(Pool, ReusesReleasedObjectLifo) {
    Pool<Item> pool;
    Item* a = pool.acquire();
    Item* b = pool.acquire();
    ASSERT_NE(a, b);
    EXPECT_EQ(pool.available(), 0u);

    pool.release(a);
    pool.release(b);
    EXPECT_EQ(pool.available(), 2u);

    // 后进先出，对齐原项目 ObjectPool<T>.Acquire（取 Items[Count-1]）。
    EXPECT_EQ(pool.acquire(), b);
    EXPECT_EQ(pool.acquire(), a);
    EXPECT_EQ(pool.size(), 2u);
}

TEST(Pool, ReservePreallocates) {
    Pool<Item> pool;
    pool.reserve(4);
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_EQ(pool.size(), 4u);
    for (int i = 0; i < 4; ++i) EXPECT_NE(pool.acquire(), nullptr);
    EXPECT_EQ(pool.available(), 0u);
}

TEST(Pool, AcquireAfterClearWorks) {
    Pool<Item> pool;
    pool.acquire();
    pool.clear();
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_NE(pool.acquire(), nullptr);
}

TEST(Pool, ReleasingNullThrows) {
    Pool<Item> pool;
    EXPECT_THROW(pool.release(nullptr), std::logic_error);
}
