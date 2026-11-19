#include <gtest/gtest.h>

#include <string>

#include "anim/core/Hash.h"

using namespace anim;

TEST(Hash, Fnva1KnownVectors) {
    EXPECT_EQ(hashString(""), 0xcbf29ce484222325ull);
    EXPECT_EQ(hashString("a"), 0xaf63dc4c8601ec8cull);
    EXPECT_EQ(hashString("foobar"), 0x85944171f73967e8ull);
}

TEST(Hash, IsStableAndDistinct) {
    const StringId bone = hashString("LeftUpperLeg");
    EXPECT_EQ(bone, hashString(std::string("LeftUpperLeg")));
    EXPECT_NE(bone, hashString("RightUpperLeg"));
    EXPECT_NE(hashString("RootT.x"), hashString("RootQ.x"));
}

TEST(Hash, UserDefinedLiteralMatches) {
    EXPECT_EQ("Bone"_sid, hashString("Bone"));
}
