#ifndef __PLATFORM_CCGL_H__
#define __PLATFORM_CCGL_H__
/// @cond DO_NOT_SHOW

#include "platform/CCPlatformConfig.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_MAC
#include "platform/mac/CCGL-mac.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_IOS
#include "platform/ios/CCGL-ios.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
#include "platform/android/CCGL-android.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
#include "platform/win32/CCGL-win32.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_WINRT
#include "platform/winrt/CCGL.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_LINUX
#include "platform/linux/CCGL-linux.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_TIZEN
#include "platform/tizen/CCGL-tizen.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
#include "platform/emscripten/CCGL-emscripten.h"
#endif

/// @endcond
#endif /* __PLATFORM_CCPLATFORMDEFINE_H__*/
