#pragma once

#include <span>

// C++20 已提供 std::span；此别名让上层代码统一使用 anim::Span。
//
// 文档：docs/02-核心数据类型.md §9
namespace anim {

template <typename T, std::size_t Extent = std::dynamic_extent>
using Span = std::span<T, Extent>;

using ByteSpan = Span<const unsigned char>;

}  // namespace anim
