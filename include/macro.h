#pragma once

#include <assert.h>

#if defined __GNUC__ || defined __llvm__
// LIKCLY ��ķ�װ, ���߱������Ż�,��������ʳ���
#define ZZ_LIKELY(x) __builtin_expect(!!(x), 1)
// LIKCLY ��ķ�װ, ���߱������Ż�,��������ʲ�����
#define ZZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define ZZ_LIKELY(x) (x)
#define ZZ_UNLIKELY(x) (x)
#endif

// ���Ժ��װ
#define ZZ_ASSERT(x)                                                           \
  if (ZZ_UNLIKELY(!(x))) {                                                     \
    ZZ_LOG_ERROR(ZZ_LOG_ROOT()) << "ASSERTION: " #x << "\nbacktrace:\n"        \
                                << zz::BackTraceToString(100, 2, "    ");      \
    assert(x);                                                                 \
  }

// ���Ժ��װ
#define ZZ_ASSERT2(x, w)                                                       \
  if (ZZ_UNLIKELY(!(x))) {                                                     \
    ZZ_LOG_ERROR(ZZ_LOG_ROOT()) << "ASSERTION: " #x << "\n"                    \
                                << w << "\nbacktrace:\n"                       \
                                << zz::BackTraceToString(100, 2, "    ");      \
    assert(x);                                                                 \
  }
