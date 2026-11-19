#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include "anim/integration/JsonWriter.h"

using namespace anim;

namespace {

namespace fs = std::filesystem;

class TempDir {
public:
    TempDir() {
        static int counter = 0;
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        _path = fs::temp_directory_path() /
                ("anim_json_writer_test_" + std::to_string(stamp) + "_" +
                 std::to_string(counter++));
        std::error_code ec;
        fs::remove_all(_path, ec);
        fs::create_directories(_path, ec);
        EXPECT_FALSE(ec) << ec.message();
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(_path, ec);
    }

    const fs::path& path() const { return _path; }

private:
    fs::path _path;
};

std::string readBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

TEST(JsonWriter, EscapesControlCharactersAndQuotes) {
    std::string input = "a\"b\\c\b\f\n\r\t";
    input.push_back('\0');
    input.push_back('\x1f');
    input += "中";

    EXPECT_EQ(json::escape(input),
              "a\\\"b\\\\c\\b\\f\\n\\r\\t\\u0000\\u001f中");
}

TEST(JsonWriter, PreservesPrintableUtf8) {
    EXPECT_EQ(json::escape("Plain / path / 角色"), "Plain / path / 角色");
}

TEST(JsonWriter, FormatsFiniteNumbers) {
    EXPECT_EQ(json::number(0.0f), "0");
    EXPECT_EQ(json::number(-0.0f), "0");
    EXPECT_EQ(json::number(1.25f), "1.25");
    EXPECT_EQ(json::number(-3.5f), "-3.5");
}

TEST(JsonWriter, FormatsDoubleNumbers) {
    EXPECT_EQ(json::number(0.0), "0");
    EXPECT_EQ(json::number(-0.0), "0");
    EXPECT_EQ(json::number(0.1), "0.10000000000000001");
    EXPECT_EQ(json::number(1.0 / 3.0), "0.33333333333333331");
}

TEST(JsonWriter, ReplacesNonFiniteNumbersWithZero) {
    EXPECT_EQ(json::number(std::numeric_limits<float>::quiet_NaN()), "0");
    EXPECT_EQ(json::number(std::numeric_limits<float>::infinity()), "0");
    EXPECT_EQ(json::number(-std::numeric_limits<float>::infinity()), "0");
}

TEST(JsonWriter, WritesBinaryFileAndCreatesParentDirectories) {
    TempDir temp;
    const fs::path path = temp.path() / "nested" / "report.json";
    const std::string text("{\"ok\":true}\n\0tail", 17);

    std::string error;
    ASSERT_TRUE(json::writeFile(path.string(), text, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(readBytes(path), text);
}

TEST(JsonWriter, TruncatesExistingFile) {
    TempDir temp;
    const fs::path path = temp.path() / "report.json";
    std::string error;

    ASSERT_TRUE(json::writeFile(path.string(), "first payload", error)) << error;
    ASSERT_TRUE(json::writeFile(path.string(), "second", error)) << error;
    EXPECT_EQ(readBytes(path), "second");
}
