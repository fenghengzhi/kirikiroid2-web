#ifndef __PLATFORM_CCSTDC_H__
#define __PLATFORM_CCSTDC_H__

#include "platform/CCPlatformConfig.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_MAC
#include "platform/mac/CCStdC-mac.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_IOS
#include "platform/ios/CCStdC-ios.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
#include "platform/android/CCStdC-android.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_WIN32
#include "platform/win32/CCStdC-win32.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_WINRT
#include "platform/winrt/CCStdC.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_LINUX
#include "platform/linux/CCStdC-linux.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
#include "platform/emscripten/CCStdC-emscripten.h"
#endif

#endif /* __PLATFORM_CCSTDC_H__*/
