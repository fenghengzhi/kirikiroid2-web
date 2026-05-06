# Oracle harness (Android aarch64)

Native side of the APK-launch oracle runner. `harness.cpp` +
`jni_bridge.cpp` compile into `libharness.so`, which is packaged into
`krkr2-harness.apk` (see [../harness-apk/](../harness-apk/)). Inside
the APK, `HarnessActivity` extends `Cocos2dxActivity` so cocos2d's init
chain runs in the same process. The Activity opens a `ServerSocket` on
port 5039 from Activity creation; the host connects via
`adb forward tcp:5039 tcp:5039`. Commands that need the native scene or
script engine wait/retry at the Python adapter layer until cocos2d's
GL-thread bootstrap has reached that state.

## Protocol

Startup (emitted once per connection):
```
READY <libkrkr2_base_hex> <heap_base_hex>
```

Commands (host → harness, one per line):
```
CALL <fn_hex> <ret> <nints> <int_hex>* <ndbls> <dbl_bits_hex>*
READ <addr_hex> <n_dec>
WRITE <addr_hex> <n_dec> <hex_bytes>
TJS_INIT
TJS_EXEC     <ascii_hex>
TJS_EXEC_STR <ascii_hex>
TJS_GLOBAL   <utf16le_key_hex>
TJS_RESET
STARTUP_FROM <utf8_hex_path>
QUIT
```

Responses (harness → host):
```
OK <retval_hex>          # int/uint/bool/ptr return, or TJS_INIT/TJS_GLOBAL VA
OK_DOUBLE <bits_hex>     # IEEE754 bit pattern
OK_VOID                  # void call, WRITE, TJS_EXEC, TJS_RESET, or QUIT
OK_DATA <hex_bytes>      # READ
OK_STR  <utf8_hex>       # TJS_EXEC_STR
ERR <message>
```

`ret` is one of `{int,uint,bool,ptr,double,void}`. Ints and doubles go
through AAPCS64 x0..x7 / d0..d7 using a "universal signature" function
pointer: up to 8 ints and 8 doubles, matching `arm64_abi.pack_args`.

## Building

`prebuilt/libharness.so` is **not checked in** — it's cross-compiled on
demand. CI builds it in the native x86_64 `build-legacy-harness` job of
[`.github/workflows/differential.yml`](../../../../.github/workflows/differential.yml),
uploads it as an artifact, and the arm64 `adb-frida` Redroid job
downloads that artifact before repacking the APK. Locally:

```bash
export KRKR2_LEGACY_NDK=/path/to/android-ndk-r17c
./build_legacy.sh
```

This must use Android NDK r17c with `APP_STL := gnustl_static`; modern
NDK r27/libc++ builds create a second C++ runtime with incompatible
`std::string`, RTTI, exception, and allocator ABI. `build_legacy.sh`
runs `check_harness_abi.py` after linking and rejects libc++ markers
such as `std::__ndk1` / `libc++abi`. The CMakeLists.txt here is only a
thin wrapper around the same script. The resulting `libharness.so` is
consumed by `harness-apk/build.sh`, which repacks it into
`krkr2-harness.apk`.

Boundary rule: STL object ownership never crosses the `.so` boundary.
`STARTUP_FROM` constructs a temporary gnustl `std::string` inside
`libharness.so` and passes it by `const&` to
`TVPMainScene::startupFrom`; `libkrkr2.so` must only read it during the
call.

## Running

Everything goes through `AdbHarnessEngine` in
[../adb_engine.py](../adb_engine.py). The engine:

1. `adb forward tcp:5039 tcp:5039`
2. `am start -W -n org.github.krkr2/.HarnessActivity`
3. Retries TCP connect until `HarnessActivity`'s `ServerSocket` binds.
4. Reads the `READY` line and starts issuing RPC commands.

See [../README.md](../README.md) for end-to-end provisioning
(`adb install krkr2-harness.apk`, pushing libkrkr2/libSDL2/libffmpeg)
and how the scalar `run_*_adb.py` drivers plus the `motion_playback`
recorder sit on top.

## Relationship with the Frida tracer

This harness handles the call/return path only — it serves `CALL` /
`READ` / `WRITE` / `TJS_*` commands, serialises a scalar/void/double
result back, and doesn't observe the target function's internal call
graph.

The Frida tracer (see [../README.md](../README.md) and
[../frida_tracer.py](../frida_tracer.py)) attaches to the
`HarnessActivity` process from the host and installs
`Interceptor.attach` hooks on a curated list of libkrkr2.so offsets.
The two layers don't know about each other at runtime — Frida just
happens to be inside the harness's address space when the host sends
the `CALL` that triggers the target function.

Division of labour:

| Layer | Asserts on | Runs when |
|---|---|---|
| ADB harness | return value + output buffer contents | every ADB test |
| Frida tracer | sub-call sequence, addresses, register snapshots at entry | `--trace` / `--record-trace` for the scalar families |
| Frida motion tracer | Motion.Player per-frame state during natural GL-thread playback | `run_motion_playback.py --record-oracle` |
