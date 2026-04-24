APP_ABI := arm64-v8a
APP_PLATFORM := android-24
APP_STL := gnustl_static
APP_CPPFLAGS := -std=gnu++11 -frtti -fexceptions -fvisibility=hidden
APP_LDFLAGS := -Wl,--exclude-libs,ALL
NDK_TOOLCHAIN_VERSION := 4.9
