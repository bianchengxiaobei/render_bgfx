#pragma once

#include "core/log.h"

#if !defined(NDEBUG) || defined(HBG_FORCE_ASSERT)
#define HBG_ASSERT(condition)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            HBG_LOGF("assertion failed: {}", #condition);                                                              \
        }                                                                                                              \
    } while (false)
#else
#define HBG_ASSERT(condition) ((void)0)
#endif

#define HBG_VERIFY(condition) HBG_ASSERT(condition)
