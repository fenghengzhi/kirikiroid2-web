# Player links, synthetic root, and variable-scope deque: four-reference comment migration (2026-08-15)

## Scope

This vertical rechecked the current Android arm64, Android armv7, iOS arm64,
and iOS armv7 MotionPlayer binaries before removing the remaining
single-`libkrkr2.so` addresses and one-target layout claims from the compiled
source comments. It did not treat older source comments as evidence.

The source logic was already aligned in this cluster. The resulting changes are
evidence/comment migration only: constructor-owned synthetic-root creation,
the two non-owning `Player` links, type-3 and particle child publication, and
the `VariableLabelScope` deque retain their existing behavior.

## Function map

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_ctor_guess` | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |
| `Player_initNodeFields_guess` | `0x6B1058` | `0x580FA4` | `0x100108720` | `0x105E70` |
| `Player_updateParticleSystems_guess` | `0x6BC4BC` | `0x588A48` | `0x100111D08` | `0x10F51C` |
| `Player_initVariables_guess` | `0x6CAB30` | `0x592944` | `0x10011D540` | `0x11BF04` |
| `Player_finalizeParameterTable_guess` | `0x6AF2AC` | `0x57FF44` | `0x1001072C4` | `0x1047FC` |

Names ending in `_guess` remain recovery names because the shipped binaries do
not provide authoritative C++ symbols for them.

## Constructor-owned links and synthetic root

The first two machine words of every native `Player` have the same source-level
meaning:

```text
rootPlayer   = this
parentPlayer = null
```

The stores are visible at Android arm64 `0x6CC150`, Android armv7 `0x5935E6`,
iOS arm64 `0x10011EC30`, and iOS armv7 `0x11D4B0..0x11D4B6`. The offsets are
therefore `+0/+8` on the 64-bit targets and `+0/+4` on the 32-bit targets. A
compiled-source comment that names only `Player+0/+8` incorrectly turns the
64-bit ABI into a cross-platform layout claim.

The same constructor also appends the sole synthetic root `MotionNode` and
copies the four process-wide default transform-order integers into that new
node. The append/copy tails begin at Android arm64 `0x6CC55C`, Android armv7
`0x5938A0`, iOS arm64 `0x10011EEF4`, and iOS armv7 `0x11D976`. Native node sizes
are 2632, 2272, 2648, and 2228 bytes respectively, so the portable source should
express this as construction plus `deque::emplace_back`, not as a common byte
offset or a separately owned runtime helper.

## Child publication protocol

Both child-producing paths replace the constructor seed with the same pair:

```text
child.rootPlayer   = parent.rootPlayer
child.parentPlayer = parent
```

For type-3 nodes, the stores immediately follow the child constructor:

| target | constructor call | two-link store |
|---|---:|---:|
| Android arm64 | `0x6B17AC` | `0x6B17B0..0x6B17BC` |
| Android armv7 | `0x5812BE` | `0x5812C6..0x5812CC` |
| iOS arm64 | `0x100108B80` | `0x100108B84..0x100108B88` |
| iOS armv7 | `0x106336` | `0x10633A..0x106350` |

The first child-specific property probe,
`motionIndependentLayerInherit`, occurs only after both stores. This ordering is
observable under re-entrant or throwing TJS getters and is why the portable
builder keeps link publication separate from the subsequent property read.

Particle children use the same pair before adaptor creation:

| target | constructor call | two-link store | adaptor wrapper |
|---|---:|---:|---:|
| Android arm64 | `0x6BCD28` | `0x6BCD2C..0x6BCD30` | `0x6BCD40` |
| Android armv7 | `0x588B8E` | `0x588B92..0x588B96` | `0x588B9E` |
| iOS arm64 | `0x1001121D4` | `0x1001121D8..0x1001121DC` | `0x1001121E8` |
| iOS armv7 | `0x10F9E2` | `0x10F9E6..0x10F9F2` | `0x10F9FA` |

The non-throwing adaptor path does not delete the supplied native child when
creation returns null. Link publication has already occurred, and the native
allocation leaks at that boundary. No rollback should be introduced merely to
make the portable code look safer.

`Player_finalizeParameterTable_guess` independently confirms the second link's
role. It iterates the calling Player's own parameter entries while advancing the
destination chain through machine word one until null. Thus the parameter
multimap is populated in the child and every ancestor; the root link is not the
parent-chain iterator.

## `VariableLabelScope` deque

All four initializers have the same high-level order:

```text
tracks.clear()
variables = selectedMotionContent["variable"]
for each reported element:
    item = variables[index]
    out = tracks.emplace_back()       // zeroed before named reads
    out.cascadeKey = string(item["label"])
    out.activeSlotCursor = 0
    out.value = 0
    out.slot[0].typeZeroFlag = true
    out.slot[1].typeZeroFlag = true
    out.frameSource = item["label"]   // independent second getter
    scope = string(item["scope"])
    if scope is non-empty:
        out.cascadeKey = scope + "::" + out.cascadeKey
```

The ABI coordinates differ:

| target | deque field in `Player` | element size | native deque object | elements per block |
|---|---:|---:|---:|---:|
| Android arm64 / libstdc++ | `+1296` | 160 | 80 | 3 |
| Android armv7 / libstdc++ | `+896` | 128 | 40 | 4 |
| iOS arm64 / libc++ | `+1152` | 160 | 48 | 25 |
| iOS armv7 / libc++ | `+812` | 128 | 24 | 32 |

The shared source-level field order is `cascadeKey`, `activeSlotCursor`,
`value`, `frameSource`, `slot[0]`, `slot[1]`. The 64-bit element is 160 bytes;
the 32-bit element is 128 bytes. The different libstdc++ and libc++ block
policies are standard-library implementation details, not a custom ring buffer.

Append-before-getter ordering is an exception boundary: if either `label`
getter, the `scope` getter, or a conversion throws, the newly appended partial
element remains in the deque with precisely the prefix of writes already
completed. Building a stack temporary and pushing only at the end would not be
equivalent.

### 2026-08-16 V148 correction and lifetime addendum

Fresh decompile plus scoped instruction reads of all four current references
correct an over-broad sentence in the original version of this note. The
initializer does **not** test the returned scope Variant for non-Void. It
unconditionally converts that Variant to `ttstr` and prefixes only if the
converted string is non-empty. Therefore both Void and an empty non-Void String
skip the prefix; the latter must produce `label`, not `"::" + label`.

The same read also recovers the nested source-owner tree omitted here: a
full-expression motion accessor owns the `variable` read and is released before
the Void gate, a function-wide list accessor owns `Count` and all numeric
reads, and one per-iteration item accessor is acquired before deque append and
owns both `label` reads plus `scope`. Ordinary post-write dispatch failures are
ignored by the typed NCB accessors. Exact four-target addresses, teardown
ordering, portable changes, and the re-entrant owner differential probe are in
`analysis/motionplayer_init_variables_nested_ncb_accessor_empty_scope_four_binary_2026-08-16.md`.

## Portable-source consequences

- `Player` retains two explicit raw, non-owning pointers. Their declaration
  order models machine words zero and one without hard-coding pointer width.
- The constructor creates exactly one synthetic root; child construction first
  creates its own root and then publishes canonical-root/immediate-parent links.
- Type-3 linking remains before the first child property read, and particle
  linking remains before NCB adaptor creation.
- Variable tracks remain `std::deque<VariableLabelScope>` with append-first
  construction, two independent `label` reads, and prefixing based on converted
  string emptiness rather than Variant type.
- Absolute addresses and per-target offsets now live in this analysis record,
  not in compiled-source comments.
