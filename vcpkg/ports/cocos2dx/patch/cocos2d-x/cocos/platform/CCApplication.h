#ifndef __PLATFORM_CCAPPLICATION_H__
#define __PLATFORM_CCAPPLICATION_H__
/// @cond DO_NOT_SHOW

#include "platform/CCPlatformConfig.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_MAC
#include "platform/mac/CCApplication-mac.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_IOS
#include "platform/ios/CCApplication-ios.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
#include "platform/android/CCApplication-android.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
#include "platform/win32/CCApplication-win32.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_WINRT
#include "platform/winrt/CCApplication.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_LINUX
#include "platform/linux/CCApplication-linux.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
#include "platform/emscripten/CCApplication-emscripten.h"
#endif

/// @endcond
#endif /* __PLATFORM_CCAPPLICATION_H__*/
