# Oracle harness (Android aarch64)

A minimal aarch64 bionic ELF that runs on a real Android emulator/device.
It `dlopen`s libkrkr2.so and then speaks a line-oriented stdin/stdout RPC
so the host-side Python driver ([adb_engine.py](../adb_engine.py)) can
inject function calls and read back results.

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

Cross-compile for aarch64 Android bionic. NDK r25+ works; we build with NDK
clang and `-fPIE -pie` (Android 5+ requires PIE).

```bash
export ANDROID_NDK=~/Library/Android/sdk/ndk/27.0.12077973
CLANG="$ANDROID_NDK/toolchains/llvm/prebuilt/$(ls $ANDROID_NDK/toolchains/llvm/prebuilt)/bin/aarch64-linux-android24-clang"
"$CLANG" -O2 -Wall -fPIE -pie harness.c -o prebuilt/harness-aarch64 -ldl
```

Or via the CMake file here, with the NDK toolchain file.

## Running

The host drives everything through `adb_engine.py`. Manual smoke test:

```bash
# one-time: push libs + harness to device
adb push prebuilt/harness-aarch64 /data/local/tmp/
adb push ../../../reference/libkrkr2/libkrkr2.so /data/local/tmp/
adb push libSDL2.so libffmpeg.so /data/local/tmp/   # extracted from apk/base.apk

# run a single QUIT to verify dlopen works
printf 'QUIT\n' | adb shell 'cd /data/local/tmp && LD_LIBRARY_PATH=. ./harness-aarch64 /data/local/tmp/libkrkr2.so'
# expect: READY <so_base> 50000000 ; OK_VOID
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
