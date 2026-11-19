#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// 代际句柄：{index, version}。删除后 version 自增，旧句柄自动失效。
//
// 模式参考 E:\Learn\hdrp-bgfx\src\core\handle.h（RenderGraphTextureHandle 等），
// 适配本项目的命名（version 对应其 generation）。
namespace anim {

template <typename Tag>
struct Handle {
    static constexpr uint32_t kInvalid = UINT32_MAX;

    uint32_t index = kInvalid;
    uint32_t version = 0;

    constexpr bool valid() const noexcept { return index != kInvalid; }
    constexpr explicit operator bool() const noexcept { return valid(); }

    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

struct HandleHash {
    template <typename Tag>
    std::size_t operator()(Handle<Tag> handle) const noexcept {
        const uint64_t value = (static_cast<uint64_t>(handle.version) << 32) | handle.index;
        return std::hash<uint64_t>{}(value);
    }
};

}  // namespace anim
