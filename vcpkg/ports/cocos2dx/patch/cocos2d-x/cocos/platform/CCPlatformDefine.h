#ifndef __PLATFORM_CCPLATFORMDEFINE_H__
#define __PLATFORM_CCPLATFORMDEFINE_H__
/// @cond DO_NOT_SHOW

#include "platform/CCPlatformConfig.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_MAC
#include "platform/mac/CCPlatformDefine-mac.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_IOS
#include "platform/ios/CCPlatformDefine-ios.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
#include "platform/android/CCPlatformDefine-android.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
#include "platform/win32/CCPlatformDefine-win32.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_LINUX
#include "platform/linux/CCPlatformDefine-linux.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
#include "platform/emscripten/CCPlatformDefine-emscripten.h"
#endif

/// @endcond
#endif /* __PLATFORM_CCPLATFORMDEFINE_H__*/
