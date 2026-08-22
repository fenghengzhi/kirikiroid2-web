# D3DEmotePlayer dead `pass(double)` convenience removal

## Conclusion

Fresh four-reference D3DEmotePlayer registrar, target-body, and target-xref
reads confirm two adjacent but disjoint typed members:

```text
script pass()       -> D3DEmotePlayer_passTimelines_guess()
script progress(dt) -> D3DEmotePlayer_progress_guess(double)
```

`pass()` performs the timeline flush and never progresses frame time.
`progress(double)` gates exact zero and then enters the shared Engine progress
core. There is no native `pass(double)` overload forwarding to `progress`.

The local `D3DEmotePlayer::pass(double)` did exactly that invented forwarding.
It was unregistered, had no production caller, and was used only three times in
one unit test. It has been removed; those calls now name `progress(double)`
directly.

## Four-reference mapping

| Role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| D3D registrar | `0x52E8E4` | `0x494078` | `0x100232278` | `0x230F46` |
| `pass` descriptor anchor | `0x52FAFC` | `0x4944BA` | `0x10023296C` | `0x2315C2` |
| no-argument pass target | `0x530E30` | `0x495016` | `0x100233464` | `0x2321A6` |
| `progress` descriptor anchor | `0x52FB38` | `0x4944CC` | `0x10023298C` | `0x2315E0` |
| one-double progress target | `0x530E3C` | `0x49501E` | `0x100233470` | `0x2321AE` |

Android arm64 exposes two target materialization xrefs per member and iOS arm64
one. The Thumb registrars use PC-relative materialization that IDA does not
report as an ordinary xref, but fresh registrar disassembly/decompilation
passes the same two addresses to the corresponding typed creators in the same
tail order.

No target has a caller outside the D3D registrar. In particular, there is no
native forwarding call from a one-double pass overload to the progress target.

## Target bodies

The four pairs normalize exactly to:

```cpp
void passTimelines(D3DShell *self) {
    emoteObject(self->primary)->engine->passTimelines();
}

void progress(D3DShell *self, double dt) {
    if (dt != 0.0)
        emoteObject(self->primary)->engine->progress(dt);
}
```

On 64-bit targets the primary EmoteObject slot is shell `+24` and Engine is
EmoteObject `+8`; on 32-bit targets the offsets are `+16` and `+4`. Neither body
checks the raw owner chain for null. The exact-zero gate belongs only to
`progress`.

The generated typed descriptors preserve distinct arities: `pass` requires
zero parameters and `progress` requires one. Surplus arguments are accepted by
the common typed adapter family; the source methods themselves have the exact
signatures above.

## Portable source correction

- removed `void D3DEmotePlayer::pass(double)` from the class and implementation;
- changed the three frame-advance test calls to `progress(double)`;
- retained `passTimelines_guess()` as the target published under script name
  `pass` and retained `progress(double)` under script name `progress`;
- added compile-time member-pointer checks for the zero-argument and one-double
  signatures;
- extended the real registered Function-object arity/receiver regression to
  cover both adjacent members;
- annotated, force-recompiled, and saved all four recovery databases.

This removes an unregistered C++ alias without changing either script-visible
descriptor or either recovered native behavior.

## Validation

Validation includes the full motionplayer test-TU Emscripten syntax check,
final Web Debug link, a zero-match scan for `D3DEmotePlayer::pass(double)` and
`.pass(double)` calls, positive checks for `passTimelines_guess` and
`progress(double)`, scoped `git diff --check`, forced IDB readback, and in-place
saves of all four recovery databases.

