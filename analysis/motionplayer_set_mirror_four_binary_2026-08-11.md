# MotionPlayer `setMirror` four-binary recovery (2026-08-11)

## Scope

This note records a fresh four-reference recovery of the script-facing
`Motion.EmotePlayer.setMirror` method and its immediate Player/reset call
chain.  The method belongs to the Engine-sized `EmotePlayer` payload.  It is
not a `Motion.Player` script member and it does not maintain an independent
Player-level mirror field.

## Four-target map

| Target | registrar | registration site | `setMirror` body | Player flip-X setter | reset tail |
|---|---:|---:|---:|---:|---:|
| Android ARM64 | `0x67CEA8` | `0x67E2C0/0x67E2C8` | `0x66F190` | `0x6CA448` | `0x66BF6C` |
| Android ARMv7 | `0x5612E8` | `0x5617EE` | `0x55A336` | `0x5926AE` | `0x558888` |
| iOS ARM64 | `0x1001B5130` | `0x1001B58C4` | `0x1001AD644` | `0x10011D14C` | `0x1001AB03C` |
| iOS ARMv7 | `0x1B4DE0` | `0x1B54D8` | `0x1ACCEA` | `0x11BB02` | `0x1AA714` |

Plain IDA string search returned no matches because the member name is a
UTF-16 literal.  An explicit UTF-16LE byte search found exactly one occurrence
in every target, at `0x14D4070`, `0x561B0C`, `0x1019606E8`, and `0x1752A4C`
respectively.  Each occurrence has only registrar data references.

The four bodies and four reset tails were freshly decompiled.  The bodies are
now named and typed in every IDB as:

```c
void __fastcall EmoteEngine_setMirror_guess(void *self, bool mirror);
void __fastcall EmoteEngine_resetControllers_guess(void *self);
```

Fresh post-type decompilation preserves the common call chain below.

## Engine layout and common data flow

| Target | requested mirror | metadata/base mirror | XOR/changed mirror | owning Player pointer |
|---|---:|---:|---:|---:|
| Android ARM64 | `Engine+1156` | `+1157` | `+1158` | `+1064` |
| Android ARMv7 | `Engine+588` | `+589` | `+590` | `+532` |
| iOS ARM64 | `Engine+788` | `+789` | `+790` | `+696` |
| iOS ARMv7 | `Engine+404` | `+405` | `+406` | `+348` |

Ignoring ABI scheduling, all four bodies are equivalent to:

```cpp
requestedMirror = mirror;
mirrorChanged = mirror != metadataMirror;
player->setFlipX(mirrorChanged);
resetControllers();
```

The Player call is exactly the same direct root flip-X setter recovered for
the public `Motion.Player.flipX` property.  It compares the constructor-owned
root node's flip-X byte, writes it only when different, and dirties that root
delta.  There is no intermediate Player scalar, allocation, lookup, or
empty-container check.

The reset call is unconditional, including when the requested value and the
derived XOR value are unchanged.  Its large body finalizes/resets all controller
families and is also the body registered as the following `skip` member.  Thus
`setMirror` has an observable controller-flush side effect in addition to the
root flip update.

## Metadata interaction

The three Engine bytes represent two inputs and one derived value, not three
synonymous flags:

- `requestedMirror` is the last script request;
- `metadataMirror` is the motion metadata's native orientation;
- `mirrorChanged` is their XOR and is the value applied to Player root flip-X.

The metadata-load path independently refreshes `metadataMirror`, recomputes
the XOR from the retained request, calls the same Player flip-X setter, and
then calls the same complete controller-reset body directly before evaluating
the Player at zero. It does not call the script `setMirror` wrapper, but the
reset side effect is nevertheless present. The earlier contrary sentence was
an obsolete conclusion from the single-`libkrkr2.so` analysis and was corrected
after fresh four-reference decompilation of the metadata application bodies.

## Boundary and ABI behavior

For ordinary TJS Boolean input all four targets agree exactly.  At the raw ABI
boundary, AArch64 Android materializes a one-bit Boolean before comparison,
while the other decompilations expose an integer/byte store spelling.  The NCB
conversion canonicalizes script Boolean arguments, so this difference is not
reachable through the registered method.  Directly invoking a recovered body
with a noncanonical raw integer would be ABI-dependent and is not modeled as a
script contract.

## Pre-edit local comparison

The local Engine method already stores the requested byte, derives the XOR,
and unconditionally calls the controller reset.  However, it delegates through
a port-only `Player::setMirror` method and `_rootFlipX` scalar.  That scalar has
no counterpart in any of the four bodies and can become stale when the public
`flipX` property changes the actual root node.  Its early-return and empty-node
guard can therefore suppress a required native root update.

The same disproved scalar was also reapplied during node-tree load and before
every root evaluation.  Because all four Players retain the constructor-created
root and the canonical setter writes that root directly, those assignments can
overwrite legitimate public `flipX` changes and have no native data-flow edge.

The faithful reconstruction is to call the already recovered
`Player::setFlipX` function directly from both Engine mirror-derivation sites,
then remove the redundant `Player::setMirror` method and `_rootFlipX` field.
The two shadow-reapplication sites must disappear with the field.  The Engine's
three distinct bytes and unconditional reset behavior remain.

## Applied reconstruction and verification

The Engine wrapper is now `setMirror_guess` and calls the canonical
`Player::setFlipX` directly.  The metadata replacement path uses the same
setter and now also takes the directly recovered reset tail. The port-only
`Player::setMirror`, its `_rootFlipX` scalar, the node-tree-load writeback and
the per-evaluation writeback were removed. `resetControllers_guess` remains
unconditional and is shared by `setMirror`, metadata replacement and the
script-facing `skip` member.

The EmotePlayer header's stale duplicated numbering was corrected: after
`variableKeys` #49 and `animating` #50, this group is `setMirror` #51
through `getCommandList` #70, matching the four registrar orders.

A focused Catch2 case creates an Engine, independently sets Player flip-X,
then requests a metadata-relative false mirror.  It checks that the actual root
is cleared, the requested/derived Engine bytes are correct, and the controller
reset still runs.  This is the exact sequence the removed Player shadow used to
handle incorrectly.

All four IDBs now save:

- `EmoteEngine_setMirror_guess` and
  `EmoteEngine_resetControllers_guess` with source-level void/Boolean types;
- `aSetMirror_utf16_guess` for the member-name literal;
- fresh post-type decompilation of every setMirror body.

After the source edit, both the Web Debug application and Wasmtime headless
guest rebuild and link successfully.  `git diff --check` reports no whitespace
errors.  The focused native Catch2 result is recorded after the first-time
Windows vcpkg test configuration completes.
