/* jni_bridge.cpp — bridges HarnessActivity's JNI entry point into the
 * C++ RPC loop in harness.cpp.
 *
 * The APK path is the only supported launch mode: HarnessActivity
 * (inside the repacked krkr2-harness.apk) extends Cocos2dxActivity, so
 * cocos2d's init chain runs in the same process. The Activity accepts a
 * TCP connection on port 5039 and hands the fd to this JNI export. */

#include <jni.h>

extern "C" int harness_rpc_main_fd(const char *so_path, int fd);

extern "C" JNIEXPORT jint JNICALL
Java_org_github_krkr2_HarnessActivity_runRpcServeFd(JNIEnv *, jclass, jint fd) {
    return harness_rpc_main_fd("libkrkr2.so", static_cast<int>(fd));
}
