# Shared chain builder: nested two-segment state and sparse triple publication

This is a fresh builder-level audit of the shared `hairControl` / `partsControl`
path using only the four current files under `reference/binaries/`. It extends
the earlier entry-owner/constructor work: that work established ownership and
destruction, while this note closes the complete metadata data flow, publication
order, type-tag split, duplicate-key behavior, and construction-level tests.

## Four-reference matrix

| reference | shared builder | size | spring allocation/call |
|---|---:|---:|---:|
| Android arm64-v8a | `0x668DB0` | `0xDAC` | `0xB0` at `0x668FBC`, ctor `0x668FC8` |
| Android armeabi-v7a | `0x556B84` | `0x5C0` | `0xA8` at `0x556CCE`, ctor `0x556CD4` |
| iOS arm64 | `0x1001A87C0` | `0x700` | `0xB0` at `0x1001A88F0`, ctor `0x1001A88FC` |
| iOS armv7 | `0x1A7DCC` | `0x6BE` | `0xA8` at `0x1A7F12`, ctor `0x1A7F1C` |

The two `applyMetadata` calls pass the same builder different deque references
and fixed type tags:

- `hairControl` -> deque #2 -> type `1`;
- `partsControl` -> deque #3 -> type `2`.

Both deques use the same source-level element type and spring type. The spring
is 176 bytes on the 64-bit references and 168 bytes on the 32-bit references.
The owning entry is 56 bytes on 64-bit and 32 bytes on 32-bit.

## Exact metadata data flow

The common loop snapshots `Count` once. It retrieves rows by the original loop
index and reads `enabled`; disabled rows are skipped before `param`, allocation,
or any coefficient read. Consequently, the deque is compact while every
published reference retains its sparse original metadata index.

For one enabled row the order is:

1. Read nested `param`.
2. Allocate `EmoteBustChainSpring` and invoke its real argument constructor on
   the **outer row**. The constructor initializes the physics coefficients from
   `gravity`, `friction_x`, `friction_y`, `b_rate`, `v_bound`, `ud_eft`,
   `bend_spd`, `bend_vol`, `length`, `scale_x`, and `scale_y`.
3. Overwrite `op`, `ofs`, `bendR`, and `bendS` from `param` in that order.
4. Retain the `bp`, `p`, and `pv` array dispatches in that construction order.
5. Decode and overwrite the six two-segment vectors in this write order:
   `p[0]`, `p[1]`, `pv[0]`, `pv[1]`, `bp[0]`, `bp[1]`. Every vector reads
   dictionary x/y/z and narrows double to float.
6. Append the raw spring pointer to the supplied deque.
7. Assign `baseLayer`, `var_lr`, `var_lrm`, and `var_ud` to the new entry.
8. Publish `{type=callerTag,index=originalMetadataIndex}` under `var_lr`, then
   `var_lrm`, then `var_ud`.

Representative triple-publication sites are:

| reference | `var_lr` | `var_lrm` | `var_ud` |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6696F0` | `0x669704` | `0x669730` |
| Android armeabi-v7a | `0x55707C` | `0x55708C` | `0x557098` |
| iOS arm64 | `0x1001A8DA8` | `0x1001A8DBC` | `0x1001A8DCC` |
| iOS armv7 | `0x1A83BA` | `0x1A83CE` | `0x1A83E2` |

No empty, equality, or duplicate-key gate exists. Later metadata overwrites an
earlier map value, and within one row the publication order means `var_ud` wins
over `var_lrm`, which wins over `var_lr`, when keys collide. Map overwrite does
not remove any previously appended entry or spring owner.

## Raw entry construction and deque policies

The raw append copies but does not clear its source pointer. It initializes the
owner, four empty strings, and two zero anchors. It does **not** initialize
`initFlag` or the ABI bytes between that byte and the first string.

| reference | entry | block/capacity | append implementation |
|---|---:|---:|---|
| Android arm64-v8a | 56B | 504 / 9 | inline fast/boundary around `0x669428` |
| Android armeabi-v7a | 32B | 512 / 16 | inline fast `0x556F1A`, boundary helper `0x56743E` |
| iOS arm64 | 56B | 4088 / 73 | helper `0x1001A912C` |
| iOS armv7 | 32B | 4096 / 128 | helper `0x1A8790` |

The large Android/iOS block-capacity difference is an STL ABI policy, not a
different source element. Compile-time checks now pin both the spring object and
entry object to their pointer-width-dependent reference sizes.

## Lifetime and partial-commit boundaries

- `param` lookup occurs before allocation.
- A spring-constructor throw is covered by ordinary new-expression allocation
  rollback.
- After successful construction and before entry construction, the spring is
  held only by a raw local. Any `op/ofs/bendR/bendS/bp/p/pv` lookup, element
  lookup, vec3 conversion, or deque-growth failure in that interval leaks it.
- Successful raw append transfers ownership. Later label assignment or any of
  the three map operations can throw, leaving a partially populated but owned
  deque entry and all earlier map writes intact.
- The three retained array Variants are destroyed in reverse construction order
  after successful publication or by exception unwind where applicable.

## Wide-string evidence

Because some IDA string renderings truncate UTF-16 values to `p` or `v`, the
relevant full keys were also checked with exact UTF-16LE byte searches:

| key | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `param` | `0x14D392A` | `0xD843FA` | `0x10195FD30` | `0x1752094` |
| `pv` | `0x14D3936` | `0xD84406` | `0x10195FD46` | `0x17520AA` |
| `ofs` | `0x14D393C` | `0xD8440C` | `0x10195FD4C` | `0x17520B0` |
| `bendR` | `0x14D3974` | `0x5572D8` | `0x10195FD84` | `0x17520E8` |
| `bendS` | `0x14D3980` | `0x5572E4` | `0x10195FD90` | `0x17520F4` |
| `bp` | `0x15033E0` | `0xD84444` | `0x10195FD9C` | `0x1752100` |
| `baseLayer` | `0x14D3944` | `0xD84414` | `0x10195FD54` | `0x17520B8` |
| `var_lrm` | `0x14D398C` | `0x557300` | `0x10195FDA2` | `0x1752106` |
| `var_ud` | `0x14D3966` | `0xD84436` | `0x10195FD76` | `0x17520DA` |

Android arm64 deliberately reuses a separate `bp` literal outside the otherwise
contiguous metadata cluster; xrefs at `0x6690B0/0x6690C0` lead back into this
builder. This is why inferring the key only from physical string adjacency would
be unsafe.

## Web reconstruction and regression scope

The builder now uses semantic count/index/metadata/parameter/vector/segment/ref
names and documents the retained-array order, ownership hand-off, untouched init
byte, and last-write-wins triple publication. New tests cover:

- a malformed disabled hole;
- outer-row physics coefficients versus nested dynamic-state overwrites;
- all six two-segment vec3 writes;
- independent entry owners and zero anchors;
- cross-colliding, all-equal, and empty keys;
- sparse type-1 references and a separate type-2 invocation;
- placement construction over nonzero storage proving `initFlag` is untouched.

Verification completed on 2026-08-15:

- Emscripten syntax compilation of the complete motionplayer Catch2 translation
  unit passed; the only diagnostic was the repository's pre-existing `_tss`
  literal-operator warning.
- CMake preset `Web Debug Build` passed through the final `index.html`/wasm link
  and shell preallocation synchronization; emitted diagnostics were pre-existing
  compiler/linker warnings.
- All four recovery IDBs were saved after semantic local renames, builder/array
  order/owner/publication comments, and raw-append bookmarks were added.

## 2026-08-16 shared vec3 helper addendum

The helper used for every `op`, `p[0/1]`, `pv[0/1]`, and `bp[0/1]` conversion has now been freshly recovered
and renamed in all four IDBs as `springVec3FromVariant_guess`. It copies its input Variant into a local
`ncbPropAccessor`, reads hinted x/y/z TJS reals, narrows them to floats, and releases the accessor. The portable
Chain calls now use that helper. This does **not** close the Chain outer builder's root/row/param/array accessor
identity or `var_lrm` hint; those remain for the next dedicated four-reference vertical. Shared-helper evidence
is recorded in
`analysis/motionplayer_bust_builder_nested_ncb_accessor_vec3_hint_four_binary_2026-08-16.md`.

## 2026-08-16 nested owner / role-mapping closure

The Chain outer builder has now been freshly closed in all four references. The
root, outer row, metadata, param, and three simultaneously-live array sources
are real `ncbPropAccessor` owners. The three serialized arrays are deliberately
role-mapped rather than name-mapped: `bp[0/1]` writes internal `p[0/1]`,
`p[0/1]` writes internal `pv[0/1]`, and `pv[0/1]` writes internal `bp[0/1]`.
This corrects the former portable implementation, which constructed the arrays
in the right order but routed them to same-named fields. Successful cleanup is
`pv -> p -> bp -> param -> metadata accessor -> outer source -> root`.

`param/op/p/pv/ofs/baseLayer` reuse the Bust slots; `bendR/bendS/bp/var_lrm`
are Chain-only. Full addresses, UTF-16 byte evidence, lifetime boundaries,
IDB writeback, portable changes, and regression-probe coverage are recorded in
`analysis/motionplayer_chain_builder_nested_ncb_accessor_role_hint_four_binary_2026-08-16.md`.
