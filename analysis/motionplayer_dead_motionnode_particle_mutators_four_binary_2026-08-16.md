# MotionPlayer dead MotionNode particle mutators: four-binary record (2026-08-16)

## Conclusion

`MotionNode::addParticleChild` and `MotionNode::eraseParticleChild` were
unregistered C++ conveniences with zero production and test callers.  Fresh
decompilation of all four current references shows that their owner boundary is
also structurally wrong for both native mutation chains:

- the outer type-4 pass retains one Array dispatch across `add`, the post-add
  `count`, optional max-count `erase(0)`, and the decision to enter the worker;
- the out-of-line worker obtains a second independent Array owner and retains
  it across numeric lookup, conditional erase, count refresh, retry of the same
  index, and the complete second update pass.

Each deleted method instead copied `particleArrayVar`, converted and retained a
new dispatch for one operation, then released it.  Calling it from either native
chain would split the receiver lifetime and permit a re-entrant callback to
switch the later count/erase operations to a different persistent Variant.

The two high-level methods were removed.  The lower-level operations that take
an already retained dispatch remain live, as do the still-used MotionNode
count/get accessors.

## Four-reference map

| Target | outer type-4 pass | out-of-line worker | outer add | outer count | outer erase | worker erase / count refresh |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6BC4BC` | `0x6BEB84` | `0x6BD5DC` | `0x6BD5F8` | `0x6BD640` | `0x6BED10` / `0x6BED24` |
| Android armv7 | `0x588A48` | `0x58AB50` | `0x5894B6` | `0x5894CA` | `0x5894FA` | `0x58AC58` / `0x58AC66` |
| iOS arm64 | `0x100111D08` | `0x1001140C8` | `0x100112D18` | `0x100112D3C` | `0x100112D84` | `0x100114224` / `0x100114238` |
| iOS armv7 | `0x10F51C` | `0x111AF8` | `0x11069E` | `0x1106BA` | `0x1106F6` | `0x111C56` / `0x111C68` |

Addresses are evidence coordinates only and are intentionally absent from
compiled-source comments.

## Outer-pass owner and mutation sequence

Each target first copy-constructs the node's particle Array Variant, converts
it to an independently retained dispatch, and destroys the temporary Variant.
The spawn tail then performs this sequence on that same dispatch identity:

```text
argument = copy(child Player Variant)
array.FuncCall(0, "add", globalAddHint, null, 1, &argument, array)
destroy argument

if VariantObject_getCount(array) > node.maxNum:
    index = Integer(0)
    array.FuncCall(0, "erase", globalEraseHint, null, 1, &index, array)
    destroy index

if emitCount <= 1:
    stepParticleChildren(player, node)

release outer array owner at the type-4 node exit
```

Both FuncCall HRESULTs are ignored.  The count getter and callbacks can re-enter
script, but the retained receiver identity cannot switch to a replacement
stored in `node.particleArrayVar` during this scope.

## Worker owner and mutation sequence

The separate worker independently retains the Array dispatch once.  In pass 1
it uses that receiver for every flags-0 numeric lookup.  On an erase edge it:

```text
array.FuncCall(0, "erase", globalEraseHint, null, 1, &index, array)
count = VariantObject_getCount(array)
--index
```

The loop increment therefore retries the same numeric slot.  The final count is
then frozen as the upper bound of pass 2, whose numeric lookups and child updates
continue under the same receiver owner.  Release occurs only after both passes
or on exception unwind.

## Why the lower-level operations remain

`particleArrayAdd_guess(iTJSDispatch2 *, Variant)` and
`particleArrayErase_guess(iTJSDispatch2 *, index)` express exactly one script
operation while leaving owner scope to the caller.  The production outer pass
and worker call these functions with their already retained dispatch, so they
preserve the native receiver continuity.

`MotionNode::getParticleCount` and `MotionNode::getParticleChild` are a distinct
surface.  They still have Player variable-traversal callers and focused tests;
this pass does not remove or alter them.

## Source effect

Removed only:

- two declarations from `MotionNode.h`;
- two definitions from `MotionNodeBridge.cpp`.

The earlier particle-lifecycle record was also corrected so it no longer lists
the removed high-level mutators as the active add/erase surface.

## Validation

- Ordinary and `KRKR2_WASMTIME_HEADLESS` Emscripten syntax checks passed for
  the complete motionplayer unit-test translation unit; only the repository's
  existing `_tss` warning was emitted.
- The shared-header rebuild completed for both `Web Debug Build --target
  motionplayer` and `Wasmtime Headless Debug Build --target motionplayer`;
  immediate reruns reported `no work to do` for both archives.
- The first combined build command outlived its 60-second wrapper while the Web
  linker was still active.  A concurrent retry briefly failed to open
  `index.wasm`; after the earlier linker exited, no Ninja/Emscripten/wasm-ld
  process remained and the final `Web Debug Build` returned success with the
  current output up to date.
- Exact motionplayer/test searches found zero `addParticleChild` or
  `eraseParticleChild` identifiers.
- The lower-level add/erase operations retain their declarations, definitions,
  and three production calls.  MotionNode count/get retain their definitions,
  Player variable-traversal calls, and focused tests.
- Targeted `git diff --check` and trailing-whitespace checks passed.
- The outer pass and worker were force-recompiled and read back with the owner
  closure comments on all four targets, then every recovery IDB was saved.
