#pragma once

#include <cstdint>
#include <string_view>

// 64-bit FNV-1a 字符串哈希，用于 bone/clip/属性名。避免运行期大量 std::string 比较。
//
// 文档：docs/02-核心数据类型.md §5
namespace anim {

using StringId = uint64_t;

inline constexpr StringId kInvalidStringId = 0;

constexpr StringId hashString(std::string_view text) noexcept {
    // FNV-1a 64-bit
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < text.size(); ++i) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(text[i]));
        hash *= 1099511628211ull;
    }
    return hash;
}

// 允许在 switch/常量表达式里使用：hashString("Bone") 之类。
inline constexpr StringId operator""_sid(const char* text, size_t length) noexcept {
    return hashString(std::string_view(text, length));
}

}  // namespace anim
