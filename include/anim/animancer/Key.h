#pragma once

#include <cstdint>
#include <functional>

#include "anim/core/Hash.h"

// 状态字典键（对应原项目 object key）。
// 支持三类键：指针（clip/transition/state）、字符串哈希、整数。
namespace anim {

struct Key {
    static constexpr uint64_t kStringTag = 1ull << 63;
    static constexpr uint64_t kIntegerTag = 1ull << 62;
    static constexpr uint64_t kPointerTag = 1ull << 61;

    uint64_t value = 0;

    constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(const Key&, const Key&) = default;

    static Key fromPointer(const void* pointer) noexcept {
        return Key{kPointerTag | (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer)) &
                                  0x1FFFFFFFFFFFFFFFull)};
    }

    static Key fromString(StringId id) noexcept {
        return Key{kStringTag | (id & 0x1FFFFFFFFFFFFFFFull)};
    }

    static Key fromInteger(int64_t value) noexcept {
        return Key{kIntegerTag | (static_cast<uint64_t>(value) & 0x1FFFFFFFFFFFFFFFull)};
    }
};

struct KeyHash {
    std::size_t operator()(const Key& key) const noexcept {
        return std::hash<uint64_t>{}(key.value);
    }
};

}  // namespace anim
