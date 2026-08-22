# Motion.Player progress dead millisecond-convenience removal

## Conclusion

All four current references implement script `Motion.Player.progress` as one
native-instance raw wrapper followed by the native frame-unit bridge. The raw
wrapper itself converts `argv[0]` with `AsReal()` and multiplies by
`60.0 / 1000.0`; it does not call a second millisecond helper and does not
clamp negative, large, infinite, or NaN values.

The local `Player::progressMillisecondsCompat_guess(double)` was not registered
and had no production caller. Its only two callers were unit tests, and it
added a port-only `[0, 60000]` millisecond range policy before entering the
bridge with a null dispatch. It was therefore a source-structure and boundary
distortion rather than a recoverable native member, and has been removed.

## Four-reference mapping

| Role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| frame-unit bridge | `0x6CFE34` | `0x595570` | `0x1001211C0` | `0x11FF88` |
| raw script wrapper | `0x6CFE78` | `0x595598` | `0x100121204` | `0x11FFB4` |
| wrapper size | `0x170` | `0xA2` | `0xBC` | `0x8A` |

Android arm64 inlines the short bridge body into the raw wrapper, so its bridge
xref list contains only the two Engine frame-unit callers. The other three
targets retain an explicit wrapper-to-bridge code xref. This compiler choice
does not change the source-level dataflow.

Fresh bridge xrefs are:

- `EmoteEngine_progressCore_guess`, already supplying frame units;
- `EmoteEngine_applyMetadata_guess`, supplying `0.0` frame units;
- the raw Player wrapper on Android armv7 and both iOS targets, after its
  millisecond conversion; Android arm64 contains the equivalent bridge body
  inline.

No reference path performs the local convenience's negative/60000-ms test.

## Shared wrapper boundary

After native receiver resolution and the one-argument lower-bound check, the
four wrappers reduce to:

```cpp
double milliseconds = argv[0]->AsReal();
progressBridge(player, objthis, milliseconds * 60.0 / 1000.0);
return TJS_S_OK;
```

They leave `result` untouched, ignore surplus parameters, do not test motion
content, and do not sanitize the floating-point input. The bridge temporarily
stores the raw dispatch, progresses/evaluates/renders bounds, dispatches queued
events, and clears the raw field only on normal return.

A separate public C++ `progressMillisecondsCompat_guess` would therefore add a
call edge and an input policy not present in the recovered source graph.

## Portable source correction

- removed the convenience declaration and definition;
- changed the two tests to call
  `progressFrames_guess(nullptr, 16.0 * kMotionFramesPerMillisecond)`
  explicitly, so the unit transition is visible at the test site;
- retained `progressCompatMethod` as the real registered raw callback and
  `progressFrames_guess` as the native-shaped frame-unit bridge;
- annotated and force-recompiled all four raw wrappers, then saved all four
  recovery databases.

This deletion does not alter the registered script surface. It removes only an
unregistered, test-only port member whose range behavior contradicted the
references.

## Validation

The vertical is validated by a full motionplayer test-TU Emscripten syntax
check, final Web Debug link, zero compiled-source/test matches for
`progressMillisecondsCompat_guess`, scoped `git diff --check`, forced IDB
readback, and in-place saves of all four recovery databases.

