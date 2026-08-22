# MotionPlayer type-4 particle child lifecycle — four-reference reconstruction (2026-08-12)

## Scope and authority

This note replaces the touched `libkrkr2.so`-only particle comments with a
current comparison of all four binaries under `reference/binaries/`.  It covers
the type-4 node's retained TJS Array, native-`Player` element unwrapping, child
creation/adaptor ownership, maximum-count eviction, the out-of-line two-pass
delete/update worker, and the observable malformed-value boundaries.

The four binaries agree on the source-level control flow.  Target-specific
addresses and layouts are recorded here rather than in compiled source.

## Function map

| Role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_updateLayers_guess` | `0x6B871C` | `0x5856E0` | `0x10010E544` | `0x10BE5C` |
| type-6 emitter pass | `0x6BC1B0` | `0x588820` | `0x100111A6C` | `0x10F2CC` |
| type-4 particle-system pass | `0x6BC4BC` | `0x588A48` | `0x100111D08` | `0x10F51C` |
| Array `count` helper | `0x56CA74` | `0x4BEB84` | `0x1000F30F4` | `0xEF8B4` |
| Array element -> native `Player *` | `0x6BEA58` | `0x58AAB0` | `0x100113FE4` | `0x1119DC` |
| delete + step particle children | `0x6BEB84` | `0x58AB50` | `0x1001140C8` | `0x111AF8` |
| following update phase | `0x6BD908` | `0x589C00` | `0x100113024` | `0x110908` |

The former armv7/iOS guesses `0x585100`, `0x10010DE8C`, and `0x10B774`
are not element-unwrapping helpers.  They are the per-`Player` random wrapper
repeatedly called by particle math.  The corrected element helpers above are
called by both the type-4 pass and the two-pass worker.

## Target layouts used by this vertical

These offsets are ABI observations, not portable source declarations.

| Field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| node particle Array Variant | `+2296` | `+1968` | `+2312` | `+1932` |
| node mesh-combine byte | `+1963` | `+1695` | `+1979` | `+1659` |
| node mesh ancestor pointer | `+1968` | `+1700` | `+1984` | `+1664` |
| node delete-outside byte | `+2188` | `+1868` | `+2204` | `+1832` |
| child playing byte | `+1099` | `+751` | `+987` | `+687` |

The outer particle pass retains the node Array dispatch once per type-4 node.
The out-of-line delete/update worker independently retains that same node
Variant again for the duration of both of its passes.

## Persistent TJS member hints

The `add` and `erase` call sites use two adjacent process-wide mutable hint
slots.  Every `erase` in the maximum-count path and in the two-pass worker uses
the same erase slot.

| Hint | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `add` | `0x1AB543C` (`variableTrackEasingHintBlock_guess[227]`) | `0x11118D8` | `0x101B69904` | `0x187D5A8` |
| `erase` | `0x1AB5440` (`variableTrackEasingHintBlock_guess[228]`) | `0x11118DC` | `0x101B69908` | `0x187D5AC` |

The Array `count` property uses a null hint, while indexed element access uses
`PropGetByNum` and therefore has no named-member hint.

## Common reconstructed pseudocode

### Retained Array wrapper and elementary operations

```text
acquireParticleArray(node.arrayVariant):
    tmp = copy(node.arrayVariant)
    force tmp to object                         // throws if non-object
    array = tmp.object
    AddRef(array)                               // one retained dispatch
    destroy tmp
    return scoped array                         // Release at scope end

particleCount(array):
    value = void
    array.PropGet(0, "count", null_hint, &value, array)
    result = value.AsInteger()                  // HRESULT is ignored
    destroy value
    return result

particleAt(array, index):
    element = void
    array.PropGetByNum(0, index, &element, array)
    dispatch = element.AsObjectNoAddRef()       // failure/void throws
    playerAdaptor = dispatch.GetNativeInstance(PlayerClassID, err=true)
                                                   // null object: No instance.
                                                   // wrong class: Invalid instance type.
    player = playerAdaptor ? playerAdaptor.native : null
    destroy element
    return player

particleAdd(array, playerVariant):
    arg = copy(playerVariant)
    array.FuncCall(0, "add", &globalAddHint, null, 1, [&arg], array)
    destroy arg

particleErase(array, index):
    arg = integer(index)
    array.FuncCall(0, "erase", &globalEraseHint, null, 1, [&arg], array)
    destroy arg
```

No helper checks a Variant type before conversion, checks the Array dispatch
for null, checks a `PropGet`/`FuncCall` HRESULT, or converts failures into a
quiet zero/null/no-op.  A successful native-instance query is nevertheless
allowed to return a null adaptor/native pointer; callers immediately
dereference the result.

### Child creation and insertion

```text
for each type-4 node:
    particleArray = acquireParticleArray(node.arrayVariant)
    childCount = particleCount(particleArray)
    ... update existing children through particleAt(particleArray, i) ...

    if emission selects a non-empty motion path:
        child = new Player(parent.resourceManager)
        child.rootPlayer = parent.rootPlayer
        child.parentPlayer = parent

        childDispatch = PlayerAdaptor.CreateAdaptor(child, sticky=false,
                                                     throwOnFailure=false)
        childVariant = childDispatch ? object(childDispatch, childDispatch)
                                     : void

        // Initialization continues even if childDispatch creation failed.
        propagate color/context/z/chara; find/play motion
        directly access child root node; initialize opacity/position/angle/zoom
        initialize velocity and damping

        particleAdd(particleArray, childVariant)
        if particleCount(particleArray) > node.maxNum:
            particleErase(particleArray, 0)      // exactly one eviction

        if emitCount <= 1:
            stepParticleChildren(parent, node)
        else:
            continue                            // skips worker this frame
```

The four adaptor wrappers all leave the output Variant void when non-throwing
adaptor creation returns null.  They do not delete the newly allocated native
`Player`; the main path continues to initialize it and inserts the void
Variant.  A later element unwrap therefore throws on that malformed entry.

### Spawned-root opacity propagation correction (2026-08-13)

Fresh four-reference comparison closes an adjacent-offset ambiguity left by
the original note. The store after `play` is **not** blend-mode propagation:

| Target | parent evaluated opacity | child root delta opacity | child delta dirty |
|---|---:|---:|---:|
| Android arm64 | `node+1576` | `root+1656` | `root+1584` |
| Android armv7 | `node+1336` | `root+1416` | `root+1344` |
| iOS arm64 | `node+1592` | `root+1672` | `root+1600` |
| iOS armv7 | `node+1304` | `root+1384` | `root+1312` |

The source-level operation is:

```text
opacity = particleNode.evaluated.opacity
if childRoot.delta.opacity != opacity:
    childRoot.delta.dirty = true
    childRoot.delta.opacity = opacity
```

The next child `updateLayers` copies the complete 0x50-byte root delta block
into the evaluated block and multiplies this value with the root timeline
opacity (`evaluated.opacity = delta.opacity * evaluated.opacity / 255`). The
prepared-render-item builder later copies that evaluated opacity. Blend mode
is an independent active-clip-slot field and is read directly by the render
item builder; there is no accumulated blend-mode member in this transform
block. The portable model and regression test were corrected accordingly.

Representative construction/adaptor call sites are:

| Target | `new` / constructor | adaptor-to-Variant wrapper |
|---|---:|---:|
| Android arm64 | `0x6BCD1C` / `0x6BCD28` | `0x6BCD40` -> `0x6EEB74` |
| Android armv7 | `0x588B88` / `0x588B8E` | `0x588B9E` -> `0x58185C` |
| iOS arm64 | `0x1001121C8` / `0x1001121D4` | `0x1001121E8` -> `0x1001092A0` |
| iOS armv7 | `0x10F9D8` / `0x10F9E2` | `0x10F9FA` -> `0x106B08` |

### Out-of-line delete/update worker

```text
stepParticleChildren(parent, node):
    array = acquireParticleArray(node.arrayVariant)  // second independent retain
    count = particleCount(array)

    for i = 0; i < count; ++i:
        child = particleAt(array, i)
        erase = !child.allPlaying
        if child.allPlaying && node.deleteOutside:
            if child bounds are valid and do not intersect parent viewport:
                erase = true
        if erase:
            particleErase(array, i)
            count = particleCount(array)
            --i

    meshParent = node.meshCombine ? &node : node.meshAncestor
    for i = 0; i < count; ++i:
        child = particleAt(array, i)
        child.cameraAngle = parent.cameraAngle
        if child.directEdit:
            child.initEmoteMotion(2)
        child.root.clipAABB = node.clipAABB
        child.root.meshAncestor = meshParent
        child.root.visibleAncestorIndex = node.visibleAncestorIndex
        child.frameProgress(parent.deltaTime)
        child.updateLayers()
        prepend child's pending-event range to parent; clear child range
```

There are no null-child, empty-node-container, lazy-tree-build, or
`updateLayers` guards in this worker.  The root node is accessed directly after
unwrapping, as it is in the existing-child transform and freshly spawned-child
initialization paths.

## Local comparison before correction

The pre-correction Web port differed at precisely the failure and ownership
boundaries:

1. `MotionNodeBridge.cpp` returned zero/null/no-op for a non-object or null
   particle Array, failed indexed property access, non-object elements, and
   invalid Player instances.
2. Its helpers borrowed `AsObjectNoAddRef()` independently on every operation;
   the native outer pass instead holds one dispatch for the whole node and its
   worker holds a second dispatch across both passes.
3. The two-pass worker was inlined into the type-4 pass despite being an
   out-of-line `(Player *, MotionNode *)` call on every target.
4. Existing-child, spawned-root, deletion, and step paths contained null/empty
   container guards absent from every reference.
5. Temporary `PARTICLESTATE` / `PARTICLECREATE` atomics and `stderr` logging
   remained in the compiled path.
6. Source comments still named the older `libkrkr2.so` functions/offsets.

The implemented source correction centralizes the strict retained-dispatch
operations, shares the two global hint slots, restores the out-of-line worker,
removes only the non-native guards and temporary instrumentation, and keeps
adaptor-failure continuation intentionally unsafe.  It also removes the extra
particle-worker `forceVisible` propagation: every reference performs exactly
three child-root stores here (clip AABB, mesh ancestor, visible ancestor).

## Source and validation result

The vertical is implemented in:

- `MotionNode.h` / `MotionNodeBridge.cpp`: retained Array scope, strict
  count/get operations, and the lower-level dispatch-based add/erase operations
  used by the owner-spanning native chains;
- `Player.h` / `PlayerUpdateParticles.cpp`: restored out-of-line
  `stepParticleChildren_guess`, one outer Array retain per type-4 node, direct
  child/root access, shared add/erase operations, and removal of temporary
  diagnostics;
- `tests/unit-tests/plugins/motionplayer-dll.cpp`: malformed Array/element,
  wrong native class, missing index, and valid Player-adaptor coverage.

Validation on 2026-08-12:

- the motionplayer unit-test translation unit passes Emscripten
  `-fsyntax-only` (only the pre-existing `_tss` literal-operator warning);
- `ninja -C out/web/debug index.html` passes;
- `ninja -C out/wasmtime/debug krkr2_wasmtime_guest` passes, including the
  exnref conversion link step;
- immediate reruns of both targets report `no work to do`;
- `git diff --check` passes and no particle temporary logging or obsolete
  `getParticleChildDispatch` symbol remains.

The 2026-08-13 opacity-field correction was revalidated with the same Web
full build, Wasmtime guest build, complete motionplayer Catch2 translation-unit
syntax check, and `git diff --check`. A narrow regression test also proves
that equality leaves child-root delta dirty unchanged and that changing only
the neighboring active-slot blend mode cannot affect this propagation.

The prepared-render builder's distinct one-owner scope across Array `count`,
all numeric child lookups, and recursive child builds was closed separately on
2026-08-14 in
`motionplayer_prepared_particle_recursion_four_binary_2026-08-14.md`. That
follow-up also records the builder's before-active placement, shared output
vectors, exact propagated arguments, and unchecked null-native call boundary.

## Fresh worker boundary follow-up (2026-08-15)

The out-of-line worker was decompiled again from all four current recovery
databases after the particle spawn/randomization audit. The source-level two-
pass reconstruction still matches, including the less defensive boundaries
below.

| boundary | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| independent Array retain | `0x6BEBC0` | `0x58AB6E` | `0x100114108` | `0x111B20` |
| initial signed count | `0x6BEC20` | `0x58AB8C` | `0x100114138` | `0x111B7A` |
| first-pass numeric get | `0x6BEC5C` | `0x58ABA6` | `0x100114170` | `0x111B90` |
| erase call | `0x6BED10` | `0x58AC58` | `0x100114224` | `0x111C56` |
| post-erase count refresh | `0x6BED24` | `0x58AC66` | `0x100114238` | `0x111C68` |
| retry-same-index adjustment | `0x6BED2C` | `0x58AC6E` | `0x10011423C` | `0x111C72` |
| mesh-parent selection | `0x6BED44..0x6BED58` | `0x58AC7E..0x58AC84` | `0x100114258..0x100114260` | `0x111C84..0x111C90` |
| second-pass numeric get | `0x6BED7C` | `0x58AC9E` | `0x100114284` | `0x111CAA` |
| frame progress / update / event transfer | `0x6BEDC4..0x6BEE1C` | `0x58ACDA..0x58ACE8` | `0x1001142E4..0x1001142F8` | `0x111D18..0x111D30` |
| receiver release | `0x6BEE34..0x6BEE44` | `0x58ACFE` onward | `0x10011430C..0x100114320` | `0x111D40..0x111D4E` |

The first-pass predicate has the following exact shape:

```text
retain if child.allPlaying and
          (!deleteOutside or
           bounds are ordered-inverted or
           bounds strictly overlap root.outsideRect)
erase otherwise
```

An inverted X or Y interval is retained before the viewport comparisons. Edge
touching fails strict overlap and is erased. NaN makes the inversion tests and
the strict-overlap conjunction false, so it is also erased. There is no finite
normalization.

After an erase call, native ignores its HRESULT, rereads signed `count`,
decrements the loop index, and lets the loop increment retry that same numeric
slot. A script receiver that does not shrink can consequently trap the worker
on the same element. No count read occurs on a retain edge.

The second pass uses the final first-pass `count` as a frozen upper bound. Its
per-child order is camera-angle copy, optional `directEdit == 1` initialization,
child-root clip/mesh/visible-ancestor stores, frame progress, layer update, and
finally parent-prepend/child-clear of the pending-event range. Re-entrant Array
mutation by any of those child calls does not refresh the upper bound; a later
numeric getter observes the mutated Array with the stale signed index range.
The independently retained Array receiver remains alive across both passes and
all exception unwinds.

All four IDBs were improved and saved with the names
`Player_updateParticleEmitters_guess`,
`Player_updateParticleSystems_guess`, `VariantObject_getCount_guess`,
`ParticleArray_getNativePlayerAt_guess`,
`Player_stepParticleChildren_guess`, `Player_random_guess`, and the relevant
Player adaptor helpers.  Fresh decompilation after saving shows those names in
the type-4 call chain on every target.

## Zero-call MotionNode mutator follow-up (2026-08-16)

The former `MotionNode::addParticleChild` and
`MotionNode::eraseParticleChild` conveniences were later found to have no
production or test callers.  Each method independently reacquired an Array
dispatch from the node's persistent Variant, so neither could represent the
outer pass's one-owner `add -> count -> optional erase` chain or the worker's
separate one-owner two-pass chain described above.  They were removed after a
fresh four-target decompile.  The lower-level `particleArrayAdd_guess` and
`particleArrayErase_guess` operations remain and receive the already retained
dispatch from the real callers.  `getParticleCount` and `getParticleChild`
also remain because Player variable traversal and focused malformed-element
tests still use them.
