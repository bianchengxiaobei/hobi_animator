#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "anim/core/Serialization.h"

using namespace anim;

TEST(Serialization, RoundTripsScalars) {
    Writer writer;
    writer.writeFileHeader(FileHeader{kMagicAB1, kCurrentFormatVersion, 7});
    writer.writeU8(0x12);
    writer.writeU16(0xBEEF);
    writer.writeU32(0xDEADBEEF);
    writer.writeU64(0x0123456789ABCDEFull);
    writer.writeI32(-1234567);
    writer.writeF32(1.25f);
    writer.writeF64(-3.5);
    writer.writeBool(true);
    writer.writeBool(false);
    writer.writeString("LeftUpperLeg");

    std::vector<uint8_t> bytes = writer.finish();
    Reader reader(bytes);

    const FileHeader header = reader.readFileHeader();
    EXPECT_EQ(header.magic, kMagicAB1);
    EXPECT_EQ(header.version, kCurrentFormatVersion);
    EXPECT_EQ(header.type, 7);
    EXPECT_EQ(reader.version(), kCurrentFormatVersion);

    EXPECT_EQ(reader.readU8(), 0x12);
    EXPECT_EQ(reader.readU16(), 0xBEEF);
    EXPECT_EQ(reader.readU32(), 0xDEADBEEFu);
    EXPECT_EQ(reader.readU64(), 0x0123456789ABCDEFull);
    EXPECT_EQ(reader.readI32(), -1234567);
    EXPECT_FLOAT_EQ(reader.readF32(), 1.25f);
    EXPECT_DOUBLE_EQ(reader.readF64(), -3.5);
    EXPECT_TRUE(reader.readBool());
    EXPECT_FALSE(reader.readBool());
    EXPECT_EQ(reader.readString(), "LeftUpperLeg");
    EXPECT_TRUE(reader.eof());
}

TEST(Serialization, LittleEndianLayout) {
    Writer writer;
    writer.writeU32(0x11223344u);
    const std::vector<uint8_t> bytes = writer.finish();
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(bytes[0], 0x44);
    EXPECT_EQ(bytes[1], 0x33);
    EXPECT_EQ(bytes[2], 0x22);
    EXPECT_EQ(bytes[3], 0x11);
}

TEST(Serialization, VectorsAndQuaternionUseUnityOrder) {
    Writer writer;
    writer.writeVec3(Vec3(1.0f, 2.0f, 3.0f));
    // Unity m_LocalRotation 按 x, y, z, w 序列化。
    writer.writeQuat(quatFromXYZW(0.1f, 0.2f, 0.3f, 0.9f));

    Reader reader(writer.data());
    EXPECT_EQ(reader.readVec3(), Vec3(1.0f, 2.0f, 3.0f));
    const Quat q = reader.readQuat();
    EXPECT_FLOAT_EQ(q.x, 0.1f);
    EXPECT_FLOAT_EQ(q.y, 0.2f);
    EXPECT_FLOAT_EQ(q.z, 0.3f);
    EXPECT_FLOAT_EQ(q.w, 0.9f);
}

TEST(Serialization, RejectsBadHeader) {
    Writer writer;
    FileHeader bad;
    bad.magic = 0x12345678u;
    writer.writeFileHeader(bad);
    Reader reader(writer.data());
    EXPECT_THROW(reader.readFileHeader(), std::logic_error);
}

TEST(Serialization, RejectsTruncatedStream) {
    Writer writer;
    writer.writeU32(42);
    Reader reader(writer.data());
    EXPECT_THROW(reader.readU64(), std::logic_error);
}

TEST(Serialization, StringTableDeduplicates) {
    StringTable table;
    const uint32_t a = table.add("Root");
    const uint32_t b = table.add("Hips");
    const uint32_t again = table.add("Root");
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    EXPECT_EQ(again, a);
    EXPECT_EQ(table.size(), 2u);
    EXPECT_EQ(table.at(b), "Hips");
    EXPECT_EQ(table.indexOf("Hips"), b);
    EXPECT_EQ(table.indexOf("Missing"), StringTable::kInvalidIndex);

    Writer writer;
    table.write(writer);

    StringTable loaded;
    Reader reader(writer.data());
    loaded.read(reader);
    EXPECT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded.at(0), "Root");
    EXPECT_EQ(loaded.at(1), "Hips");
    EXPECT_TRUE(reader.eof());
}
