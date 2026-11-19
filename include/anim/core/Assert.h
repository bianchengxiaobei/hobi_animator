#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

// 断言与错误。
//  - ANIM_ASSERT: 仅 debug（NDEBUG 下消失），用于开发期不变量。
//  - ANIM_CHECK : 始终检查；失败抛 std::logic_error（不静默）。
//
// 文档：docs/02-核心数据类型.md §6、§7
// 对齐原项目 Validate.cs 的关键校验点（root 匹配、索引有效、playable 有效等）。
namespace anim {
namespace detail {

[[noreturn]] inline void checkFail(const char* expression, const char* file, int line) {
    std::string message = "ANIM_CHECK failed: ";
    message += expression;
    message += " (";
    message += file;
    message += ":";
    message += std::to_string(line);
    message += ")";
    throw std::logic_error(message);
}

[[noreturn]] inline void checkFailMsg(const char* expression, const char* message,
                                      const char* file, int line) {
    std::string text = "ANIM_CHECK failed: ";
    text += expression;
    text += " - ";
    text += message;
    text += " (";
    text += file;
    text += ":";
    text += std::to_string(line);
    text += ")";
    throw std::logic_error(text);
}

}  // namespace detail
}  // namespace anim

#if defined(NDEBUG)
#define ANIM_ASSERT(expression) ((void)0)
#else
#include <cassert>
#define ANIM_ASSERT(expression) assert(expression)
#endif

#define ANIM_CHECK(expression)                                       \
    do {                                                             \
        if (!(expression)) ::anim::detail::checkFail(#expression, __FILE__, __LINE__); \
    } while (false)

#define ANIM_CHECK_MSG(expression, message)                                     \
    do {                                                                        \
        if (!(expression))                                                      \
            ::anim::detail::checkFailMsg(#expression, message, __FILE__, __LINE__); \
    } while (false)
