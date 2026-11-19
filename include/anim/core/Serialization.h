#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "anim/core/Assert.h"
#include "anim/core/Hash.h"
#include "anim/core/Math.h"
#include "anim/core/Span.h"

// 二进制读写（.ab 与黄金数据）。要求：
//  - 小端（与平台无关，逐字节写出）。
//  - 显式版本号。
//  - 字符串 = u32 长度 + 原始字节（不含结尾 0）。
//
// 文档：docs/02-核心数据类型.md §4、docs/03-动画数据与曲线.md §4
namespace anim {

inline constexpr uint32_t kMagicAB1 =
    uint32_t('A') | (uint32_t('B') << 8) | (uint32_t('N') << 16) | (uint32_t('1') << 24);

inline constexpr uint16_t kCurrentFormatVersion = 3;

struct FileHeader {
    uint32_t magic = kMagicAB1;
    uint16_t version = kCurrentFormatVersion;
    uint16_t type = 0;
};

class Writer {
public:
    Writer() = default;
    explicit Writer(std::size_t reserveBytes) { _buffer.reserve(reserveBytes); }

    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU32(uint32_t value);
    void writeU64(uint64_t value);
    void writeI32(int32_t value);
    void writeF32(float value);
    void writeF64(double value);
    void writeBool(bool value);

    void writeBytes(const void* data, std::size_t size);
    void writeString(std::string_view text);
    void writeFileHeader(const FileHeader& header);

    void writeVec2(const Vec2& value);
    void writeVec3(const Vec3& value);
    void writeVec4(const Vec4& value);
    // 按 Unity 的 (x, y, z, w) 顺序写出。
    void writeQuat(const Quat& value);

    template <typename T>
    void write(const T& value) {
        static_assert(std::is_arithmetic_v<T>,
                      "use writeVec2/Vec3/Vec4/writeQuat for vector types");
        if constexpr (std::is_same_v<T, float>) {
            writeF32(value);
        } else if constexpr (std::is_same_v<T, double>) {
            writeF64(value);
        } else if constexpr (std::is_same_v<T, bool>) {
            writeBool(value);
        } else {
            static_assert(sizeof(T) <= 8, "unsupported width");
            writeBytes(&value, sizeof(T));
        }
    }

    const std::vector<uint8_t>& data() const noexcept { return _buffer; }
    std::size_t size() const noexcept { return _buffer.size(); }
    void clear() noexcept { _buffer.clear(); }

    // 移动返回缓冲，之后 Writer 为空。
    std::vector<uint8_t> finish() { return std::move(_buffer); }

private:
    std::vector<uint8_t> _buffer;
};

class Reader {
public:
    explicit Reader(Span<const uint8_t> data) : _data(data) {}
    explicit Reader(const std::vector<uint8_t>& data)
        : _data(data.data(), data.size()) {}

    FileHeader readFileHeader();
    uint16_t version() const noexcept { return _version; }

    uint8_t readU8();
    uint16_t readU16();
    uint32_t readU32();
    uint64_t readU64();
    int32_t readI32();
    float readF32();
    double readF64();
    bool readBool();

    void readBytes(void* out, std::size_t size);
    std::string_view readString();

    Vec2 readVec2();
    Vec3 readVec3();
    Vec4 readVec4();
    Quat readQuat();

    template <typename T>
    T read() {
        static_assert(std::is_arithmetic_v<T>,
                      "use readVec2/Vec3/Vec4/readQuat for vector types");
        if constexpr (std::is_same_v<T, float>) {
            return readF32();
        } else if constexpr (std::is_same_v<T, double>) {
            return readF64();
        } else if constexpr (std::is_same_v<T, bool>) {
            return readBool();
        } else {
            T value{};
            readBytes(&value, sizeof(T));
            return value;
        }
    }

    bool eof() const noexcept { return _pos >= _data.size(); }
    std::size_t remaining() const noexcept { return _data.size() - _pos; }
    std::size_t position() const noexcept { return _pos; }
    void skip(std::size_t count);

private:
    void require(std::size_t count) const;

    Span<const uint8_t> _data;
    std::size_t _pos = 0;
    uint16_t _version = 0;
};

// 全局字符串表：把重复的 bone/clip/属性名去重，索引写入 .ab。
class StringTable {
public:
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    uint32_t add(std::string_view text);
    const std::string& at(uint32_t index) const;
    uint32_t indexOf(std::string_view text) const;
    std::size_t size() const noexcept { return _items.size(); }
    void clear();

    void write(Writer& writer) const;
    void read(Reader& reader);

private:
    std::vector<std::string> _items;
    std::unordered_map<std::string, uint32_t> _lookup;
};

}  // namespace anim
