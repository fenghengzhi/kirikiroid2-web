#ifndef __CC_PLATFORM_DEFINE_EMSCRIPTEN_H__
#define __CC_PLATFORM_DEFINE_EMSCRIPTEN_H__

#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include <assert.h>

#define CC_DLL

#define CC_ASSERT(cond) assert(cond)
#define CC_UNUSED_PARAM(unusedparam) (void)unusedparam

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#endif // __CC_PLATFORM_DEFINE_EMSCRIPTEN_H__
