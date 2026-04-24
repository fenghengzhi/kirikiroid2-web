LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := harness
LOCAL_SRC_FILES := harness.cpp jni_bridge.cpp
LOCAL_CPPFLAGS := -Wall -Wextra
LOCAL_LDFLAGS := -Wl,--exclude-libs,ALL
LOCAL_LDLIBS := -ldl -llog
include $(BUILD_SHARED_LIBRARY)
