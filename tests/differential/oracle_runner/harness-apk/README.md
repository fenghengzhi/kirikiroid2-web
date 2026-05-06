# Harness APK (repacked krkr2 1.4.4)

`krkr2-harness.apk` is the upstream `reference/apk/krkr2 1.4.4.apk` with:

1. A new `org.github.krkr2.HarnessActivity` class (extends `KR2Activity`)
   shipped as `classes2.dex`. Binds a TCP socket on `127.0.0.1:5039` from
   Activity creation and hands each accepted connection's fd to
   `libharness.so::harness_rpc_main_fd`.
2. `libharness.so` dropped into `lib/arm64-v8a/` alongside `libkrkr2.so`.
3. A `<activity>` entry in `AndroidManifest.xml` wiring HarnessActivity
   to an exported intent.
4. Re-signed with the test keystore at `keystore/harness-test.jks`.

The original MainActivity is untouched — install this APK and the game
still plays normally.

## Why repack instead of running alongside

`TVPInitScriptEngine` (libkrkr2 sub_8E2B28) needs cocos2d's
`applicationDidFinishLaunching` to have filled
`qword_1AF4300`/`qword_1AF31A8` first. That only happens on the GL
thread, and only after a real `Cocos2dxActivity` brings up a
`Cocos2dxGLSurfaceView`. Running libkrkr2 out-of-process (Qiling) or
under `app_process` without a Surface can't hit that init path.

By extending the real KR2Activity inside the real APK we get cocos2d's
2000 lines of initialization for free — then just bolt a socket server
on top. Commands that need TVPMainScene retry until the GL-thread
bootstrap has produced the native scene.

## Building

```bash
# Prerequisites: JDK 11+ (openjdk@21), apktool 3.x, Android SDK
# build-tools 34.0.0 at $ANDROID_HOME, and android-ndk-r17c for libharness.so.
export KRKR2_LEGACY_NDK=/path/to/android-ndk-r17c
tests/differential/oracle_runner/harness/build_legacy.sh       # builds libharness.so
tests/differential/oracle_runner/harness-apk/build.sh          # produces prebuilt/krkr2-harness.apk
```

Uses `apktool d` → compile `HarnessActivity.java` → `d8` → splice as
`classes2.dex` → `apktool b` → `zipalign` → `apksigner sign`. See
`build.sh` for specifics.

## Running locally

```bash
adb install -r -t prebuilt/krkr2-harness.apk
adb forward tcp:5039 tcp:5039
adb shell 'am force-stop org.github.krkr2'
adb shell 'am start -W -n org.github.krkr2/.HarnessActivity'

# Logcat should show:
#   HarnessRpc: listening on 127.0.0.1:5039

# Single TJS_INIT probe:
printf 'TJS_INIT\nQUIT\n' | nc 127.0.0.1 5039
# Expect: READY <so_base> 50000000 ; OK <ttjs_ptr> ; OK_VOID
```

The Python driver does all of this automatically via `AdbHarnessEngine`
(the APK path is the only supported launch mode). See
`../adb_engine.py`.

## Frida attach

Needs `adb root` + root frida-server for Android's default non-
debuggable user-installed APKs. The Redroid CI image is already
rooted; local emulators need `adb root` once.

## Keystore

`keystore/harness-test.jks` is an intentionally test-only key, valid
for 100 years. Committed to the repo because the APK is not shipping
to end users — it's a test harness. Password is in the sibling
`keystore/harness-test.password` file (plain text, no newline).
