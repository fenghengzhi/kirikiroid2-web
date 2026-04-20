/* jni_bridge.cpp — bridges Java entry points into the C++ RPC loops in
 * harness.cpp.
 *
 * Two JNI exports:
 *   - Java_org_krkr2_HarnessMain_runRpcLoop: app_process path (stdin/stdout,
 *     so_path from argv[0]).
 *   - Java_org_github_krkr2_HarnessActivity_runRpcServeFd: APK path (TCP
 *     socket fd, libkrkr2 already loaded by cocos2d so SONAME lookup). */

#include <jni.h>
#include <cstdlib>
#include <cstring>

extern "C" int harness_rpc_main(const char *so_path);
extern "C" int harness_rpc_main_fd(const char *so_path, int fd);

extern "C" JNIEXPORT jint JNICALL
Java_org_krkr2_HarnessMain_runRpcLoop(JNIEnv *env, jclass, jobjectArray argv) {
    const char *so_path = "/data/local/tmp/libkrkr2.so";
    jstring first = nullptr;
    const char *borrowed = nullptr;
    if (argv) {
        jsize n = env->GetArrayLength(argv);
        if (n > 0) {
            first = static_cast<jstring>(env->GetObjectArrayElement(argv, 0));
            if (first) {
                borrowed = env->GetStringUTFChars(first, nullptr);
                if (borrowed) so_path = borrowed;
            }
        }
    }

    int rc = harness_rpc_main(so_path);

    if (borrowed) env->ReleaseStringUTFChars(first, borrowed);
    return rc;
}

/* Serve a single RPC session on the given socket fd, inside the APK
 * process (libkrkr2 already loaded by Cocos2dxActivity via
 * System.loadLibrary("krkr2") — resolves by SONAME). */
extern "C" JNIEXPORT jint JNICALL
Java_org_github_krkr2_HarnessActivity_runRpcServeFd(JNIEnv *, jclass, jint fd) {
    return harness_rpc_main_fd("libkrkr2.so", static_cast<int>(fd));
}
