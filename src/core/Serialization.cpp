#include "anim/core/Serialization.h"

#include <limits>

namespace anim {
namespace {

void appendLE16(std::vector<uint8_t>& buffer, uint16_t value) {
    buffer.push_back(static_cast<uint8_t>(value & 0xFFu));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void appendLE32(std::vector<uint8_t>& buffer, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        buffer.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
}

void appendLE64(std::vector<uint8_t>& buffer, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8)
        buffer.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
}

}  // namespace

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

void Writer::writeU8(uint8_t value) { _buffer.push_back(value); }

void Writer::writeU16(uint16_t value) { appendLE16(_buffer, value); }

void Writer::writeU32(uint32_t value) { appendLE32(_buffer, value); }

void Writer::writeU64(uint64_t value) { appendLE64(_buffer, value); }

void Writer::writeI32(int32_t value) { appendLE32(_buffer, static_cast<uint32_t>(value)); }

void Writer::writeF32(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t));
    std::memcpy(&bits, &value, sizeof(bits));
    appendLE32(_buffer, bits);
}

void Writer::writeF64(double value) {
    uint64_t bits = 0;
    static_assert(sizeof(double) == sizeof(uint64_t));
    std::memcpy(&bits, &value, sizeof(bits));
    appendLE64(_buffer, bits);
}

void Writer::writeBool(bool value) { writeU8(value ? 1u : 0u); }

void Writer::writeBytes(const void* data, std::size_t size) {
    ANIM_CHECK(data != nullptr || size == 0);
    if (size == 0) return;
    const auto* bytes = static_cast<const uint8_t*>(data);
    _buffer.insert(_buffer.end(), bytes, bytes + size);
}

void Writer::writeString(std::string_view text) {
    ANIM_CHECK(text.size() <= std::numeric_limits<uint32_t>::max());
    writeU32(static_cast<uint32_t>(text.size()));
    if (!text.empty()) writeBytes(text.data(), text.size());
}

void Writer::writeFileHeader(const FileHeader& header) {
    writeU32(header.magic);
    writeU16(header.version);
    writeU16(header.type);
}

void Writer::writeVec2(const Vec2& value) {
    writeF32(value.x);
    writeF32(value.y);
}

void Writer::writeVec3(const Vec3& value) {
    writeF32(value.x);
    writeF32(value.y);
    writeF32(value.z);
}

void Writer::writeVec4(const Vec4& value) {
    writeF32(value.x);
    writeF32(value.y);
    writeF32(value.z);
    writeF32(value.w);
}

void Writer::writeQuat(const Quat& value) {
    writeF32(value.x);
    writeF32(value.y);
    writeF32(value.z);
    writeF32(value.w);
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

void Reader::require(std::size_t count) const {
    ANIM_CHECK_MSG(count <= _data.size() - _pos, "unexpected end of stream");
}

FileHeader Reader::readFileHeader() {
    FileHeader header;
    header.magic = readU32();
    header.version = readU16();
    header.type = readU16();
    ANIM_CHECK_MSG(header.magic == kMagicAB1, "bad .ab magic");
    // 版本兼容由具体格式（如 ClipIO）决定；这里只负责读出字段。
    _version = header.version;
    return header;
}

uint8_t Reader::readU8() {
    require(1);
    return _data[_pos++];
}

uint16_t Reader::readU16() {
    require(2);
    const uint8_t* p = _data.data() + _pos;
    _pos += 2;
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t Reader::readU32() {
    require(4);
    const uint8_t* p = _data.data() + _pos;
    _pos += 4;
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t Reader::readU64() {
    require(8);
    const uint8_t* p = _data.data() + _pos;
    _pos += 8;
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

int32_t Reader::readI32() { return static_cast<int32_t>(readU32()); }

float Reader::readF32() {
    const uint32_t bits = readU32();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double Reader::readF64() {
    const uint64_t bits = readU64();
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool Reader::readBool() {
    const uint8_t value = readU8();
    ANIM_CHECK_MSG(value <= 1, "invalid bool encoding");
    return value != 0;
}

void Reader::readBytes(void* out, std::size_t size) {
    require(size);
    if (size != 0) std::memcpy(out, _data.data() + _pos, size);
    _pos += size;
}

std::string_view Reader::readString() {
    const uint32_t length = readU32();
    require(length);
    const char* text = reinterpret_cast<const char*>(_data.data() + _pos);
    _pos += length;
    return std::string_view(text, length);
}

Vec2 Reader::readVec2() {
    // 注意：不要写成 Vec2(readF32(), readF32())，函数实参求值顺序未指定。
    const float x = readF32();
    const float y = readF32();
    return Vec2(x, y);
}

Vec3 Reader::readVec3() {
    const float x = readF32();
    const float y = readF32();
    const float z = readF32();
    return Vec3(x, y, z);
}

Vec4 Reader::readVec4() {
    const float x = readF32();
    const float y = readF32();
    const float z = readF32();
    const float w = readF32();
    return Vec4(x, y, z, w);
}

Quat Reader::readQuat() {
    const float x = readF32();
    const float y = readF32();
    const float z = readF32();
    const float w = readF32();
    return Quat(w, x, y, z);
}

void Reader::skip(std::size_t count) {
    require(count);
    _pos += count;
}

// ---------------------------------------------------------------------------
// StringTable
// ---------------------------------------------------------------------------

uint32_t StringTable::add(std::string_view text) {
    auto it = _lookup.find(std::string(text));
    if (it != _lookup.end()) return it->second;

    ANIM_CHECK_MSG(_items.size() < kInvalidIndex, "string table overflow");
    const uint32_t index = static_cast<uint32_t>(_items.size());
    _items.emplace_back(text);
    _lookup.emplace(_items.back(), index);
    return index;
}

const std::string& StringTable::at(uint32_t index) const {
    ANIM_CHECK_MSG(index < _items.size(), "string table index out of range");
    return _items[index];
}

uint32_t StringTable::indexOf(std::string_view text) const {
    auto it = _lookup.find(std::string(text));
    return it == _lookup.end() ? kInvalidIndex : it->second;
}

void StringTable::clear() {
    _items.clear();
    _lookup.clear();
}

void StringTable::write(Writer& writer) const {
    ANIM_CHECK_MSG(_items.size() <= std::numeric_limits<uint32_t>::max(),
                   "string table too large");
    writer.writeU32(static_cast<uint32_t>(_items.size()));
    for (const std::string& item : _items) writer.writeString(item);
}

void StringTable::read(Reader& reader) {
    clear();
    const uint32_t count = reader.readU32();
    _items.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const std::string_view view = reader.readString();
        _items.emplace_back(view);
        _lookup.emplace(_items.back(), i);
    }
}

}  // namespace anim
