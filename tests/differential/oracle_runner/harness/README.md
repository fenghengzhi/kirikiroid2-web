# Oracle harness (Android aarch64)

Two build flavours of the same RPC server, swappable by
`AdbHarnessEngine(launcher_mode=...)`:

1. **Standalone PIE ELF** (`prebuilt/harness-aarch64`, `launcher_mode="elf"`).
   A minimal aarch64 bionic executable. No JVM. `dlopen`s libkrkr2.so in
   its own `main()`. Fastest to boot; used for local smoke tests and the
   legacy Qiling path.
2. **app_process + JNI shared lib** (`prebuilt/libharness.so` +
   `prebuilt/harness-launcher.dex`, `launcher_mode="app_process"`, the
   default). `HarnessMain.java` starts an ART VM via
   `app_process`, reflection-calls `ActivityThread.systemMain()`, then
   `System.load`s `libharness.so` and enters the same RPC loop from
   inside the JVM process. Required when libkrkr2 needs a real Android
   runtime underneath (e.g. `TVPInitScriptEngine` reading SharedPrefs
   via JNI).

Both flavours speak the identical stdin/stdout protocol below.

## Protocol

Startup (emitted once):
```
READY <libkrkr2_base_hex> <heap_base_hex>
```

Commands (host → harness, one per line):
```
CALL <fn_hex> <ret> <nints> <int_hex>* <ndbls> <dbl_bits_hex>*
READ <addr_hex> <n_dec>
WRITE <addr_hex> <n_dec> <hex_bytes>
QUIT
```

Responses (harness → host):
```
OK <retval_hex>          # int/uint/bool/ptr return
OK_DOUBLE <bits_hex>     # IEEE754 bit pattern
OK_VOID                  # void call, WRITE, or QUIT
OK_DATA <hex_bytes>      # READ
ERR <message>
```

`ret` is one of `{int,uint,bool,ptr,double,void}`. Ints and doubles go
through AAPCS64 x0..x7 / d0..d7 using a "universal signature" function
pointer: up to 8 ints and 8 doubles, matching `arm64_abi.pack_args`.

## Building

Cross-compile via the NDK (r25+). Use clang++ — harness.cpp uses
`<exception>`. Link `-static-libstdc++` so the binary doesn't depend on
`libc++_shared.so` at runtime.

```bash
export ANDROID_NDK=~/Library/Android/sdk/ndk/27.0.12077973
CLANG="$ANDROID_NDK/toolchains/llvm/prebuilt/$(ls $ANDROID_NDK/toolchains/llvm/prebuilt)/bin/aarch64-linux-android24-clang++"

# 1. Standalone PIE ELF
"$CLANG" -O2 -Wall -fPIE -pie -static-libstdc++ \
         harness.cpp -ldl -o prebuilt/harness-aarch64

# 2. JNI shared library (for app_process launcher)
"$CLANG" -O2 -Wall -fPIC -shared -static-libstdc++ \
         harness.cpp jni_bridge.cpp -ldl -llog \
         -o prebuilt/libharness.so
```

And the Java launcher (needs JDK + Android SDK build-tools):

```bash
ANDROID_HOME=~/Library/Android/sdk
ANDROID_JAR=$(ls $ANDROID_HOME/platforms/android-*/android.jar | tail -1)
D8=$ANDROID_HOME/build-tools/34.0.0/d8

rm -rf build-java && mkdir build-java
javac --release 8 -d build-java -cp "$ANDROID_JAR" HarnessMain.java
"$D8" --release --min-api 24 --output build-java \
      build-java/org/krkr2/HarnessMain.class
cp build-java/classes.dex prebuilt/harness-launcher.dex
```

Or via the CMakeLists.txt here for the native side.

## Running

The host drives everything through `adb_engine.py`. Manual smoke tests:

```bash
# one-time: push libs + harness to device
adb push prebuilt/harness-aarch64       /data/local/tmp/
adb push prebuilt/libharness.so         /data/local/tmp/
adb push prebuilt/harness-launcher.dex  /data/local/tmp/
adb push ../../../reference/libkrkr2/libkrkr2.so /data/local/tmp/
adb push libSDL2.so libffmpeg.so /data/local/tmp/   # extracted from base.apk

# (A) Standalone ELF — fast, no JVM
printf 'QUIT\n' | adb shell \
  'cd /data/local/tmp && LD_LIBRARY_PATH=. ./harness-aarch64 /data/local/tmp/libkrkr2.so'
# expect: READY <so_base> 50000000 ; OK_VOID

# (B) app_process launch — real Android runtime
printf 'TJS_INIT\nQUIT\n' | adb shell \
  'cd /data/local/tmp && LD_LIBRARY_PATH=. CLASSPATH=harness-launcher.dex \
   app_process . org.krkr2.HarnessMain \
     /data/local/tmp/libharness.so /data/local/tmp/libkrkr2.so'
# expect: READY <so_base> 50000000 ; OK <ttjs_ptr> ; OK_VOID
```

## Relationship with the Frida tracer

This harness handles the call/return path only — it `dlopen`s
libkrkr2, dispatches `CALL` / `READ` / `WRITE` / `TJS_*` commands over
stdin, and serialises a scalar/void/double result back. It doesn't
observe the function's internal call graph.

The Frida tracer (see [../README.md](../README.md) and
[../frida_tracer.py](../frida_tracer.py)) attaches to this same harness
process from the host and installs `Interceptor.attach` hooks on a
curated list of libkrkr2.so offsets. The two layers don't know about
each other at runtime — Frida just happens to be inside the harness's
address space when the host sends the `CALL` that triggers the target
function.

Division of labour:

| Layer | Asserts on | Runs when |
|---|---|---|
| ADB harness | return value + output buffer contents | every ADB test |
| Frida tracer | sub-call sequence, addresses, register snapshots at entry | `--trace` / `--record-trace` (CI uses `--trace` on all 4 families) |
