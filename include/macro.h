#pragma once

#include <assert.h>

#if defined __GNUC__ || defined __llvm__
// LIKCLY 宏的封装, 告诉编译器优化,条件大概率成立
#define ZZ_LIKELY(x) __builtin_expect(!!(x), 1)
// LIKCLY 宏的封装, 告诉编译器优化,条件大概率不成立
#define ZZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define ZZ_LIKELY(x) (x)
#define ZZ_UNLIKELY(x) (x)
#endif

// 断言宏封装
#define ZZ_ASSERT(x)                                                           \
  if (ZZ_UNLIKELY(!(x))) {                                                     \
    ZZ_LOG_ERROR(ZZ_LOG_ROOT()) << "ASSERTION: " #x << "\nbacktrace:\n"        \
                                << zz::BackTraceToString(100, 2, "    ");      \
    assert(x);                                                                 \
  }

// 断言宏封装
#define ZZ_ASSERT2(x, w)                                                       \
  if (ZZ_UNLIKELY(!(x))) {                                                     \
    ZZ_LOG_ERROR(ZZ_LOG_ROOT()) << "ASSERTION: " #x << "\n"                    \
                                << w << "\nbacktrace:\n"                       \
                                << zz::BackTraceToString(100, 2, "    ");      \
    assert(x);                                                                 \
  }
