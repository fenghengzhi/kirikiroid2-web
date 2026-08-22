# Bust metadata builder: sparse dual publication and raw-owner hand-off

This note records a fresh four-reference audit of the metadata builder behind
`EmoteEngine::buildBustControl_guess`. It supersedes any older comments that
derived this path from `libkrkr2.so`; the evidence below comes only from the four
files currently under `reference/binaries/`.

## Function matrix

| reference | builder | size | spring allocation |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6683F8` | `0x824` | `0x48` at `0x668600` |
| Android armeabi-v7a | `0x55659C` | `0x350` | `0x48` at `0x5566A8` |
| iOS arm64 | `0x1001A7DDC` | `0x43C` | `0x48` at `0x1001A7EFC` |
| iOS armv7 | `0x1A730C` | `0x45A` | `0x48` at `0x1A744A` |

The four builders snapshot the array count once, iterate the original metadata
index, read `enabled`, and skip a disabled row before reading `param` or
constructing a spring. Enabled rows therefore produce a compact deque but retain
the original sparse metadata index in both published references.

## Data flow and ordering

For each enabled outer element, the builder performs these operations in order:

1. Read nested `param` from the outer element.
2. Allocate a 72-byte `EmoteSpringState` and call its argument constructor with
   the **outer element**, not `param`. That constructor reads `gravity`,
   `spring`, `friction`, `scale_x`, and `scale_y`, sets `firstFlag=1`, and zeros
   stored/position/velocity state.
3. Read `param.op`, `param.p`, and `param.pv`; each retained dictionary is read
   in x/y/z order and narrowed from double to float. These overwrite
   storedXYZ, posXYZ, and velXYZ respectively.
4. Read `param.ofs`, narrow it to float, and overwrite `biasY`.
5. Append the raw spring pointer to the first Engine deque.
6. Assign the new entry's `baseLayer`, `var_lr`, and `var_ud` strings in that
   order.
7. Publish `{type=0, index=originalMetadataIndex}` under `var_lr`, then publish
   the same pair under `var_ud`.

Representative publication sites are `0x6689B0/0x6689C0`,
`0x556868/0x556876`, `0x1001A816C/0x1001A817C`, and
`0x1A76DE/0x1A76F4`.

No uniqueness or emptiness check exists. Empty, equal, duplicate, and
cross-colliding keys are accepted. The later map write wins, including `var_ud`
over `var_lr` when both strings are equal, while all previously appended deque
entries and spring owners remain alive.

## Entry layout and deque blocks

The raw append operation initializes the following logical fields only:

- spring owner pointer;
- `initFlag = 1`;
- empty `baseLayer`, `var_lr`, and `var_ud` strings;
- `anchorX = anchorY = 0`.

The raw source pointer is copied and is not cleared. ABI alignment bytes between
the flag and the first string are not written.

| reference | entry stride | entries/block | allocated block | append form |
|---|---:|---:|---:|---|
| Android arm64-v8a | 48 | 10 | 480 (`0x1E0`) | inline in builder |
| Android armeabi-v7a | 28 | 18 | 504 (`0x1F8`) | helper `0x556B50` |
| iOS arm64 | 48 | 85 | 4080 | helper `0x1001A84B0` |
| iOS armv7 | 28 | 146 | 4088 | helper `0x1A7AA8` |

The different Android/iOS block policies are STL implementation details; the
logical entry fields and construction order agree.

## Lifetime and failure boundaries

- Failure while obtaining `param` occurs before allocation and therefore leaks
  no spring.
- A throw from the spring constructor is covered by ordinary C++ new-expression
  rollback.
- After the constructor returns and before deque entry construction succeeds,
  the spring exists only in a raw local. A failing `op/p/pv/ofs` property read,
  vector decode, map growth, or deque block growth in this interval leaks it in
  the references.
- Successful entry construction is the ownership-transfer boundary. A later
  string read/assignment or map operation can leave a partially populated but
  owned deque entry.
- The copied raw local remains unchanged after successful append; cleanup is
  performed only through the deque entry owner.

## String evidence

The relevant wide-string clusters are:

| key | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `param` | `0x14D392A` | `0xD843FA` | `0x10195FD30` | `0x1752094` |
| `pv` | `0x14D3936` | `0xD84406` | `0x10195FD46` | `0x17520AA` |
| `ofs` | `0x14D393C` | `0xD8440C` | `0x10195FD4C` | `0x17520B0` |
| `baseLayer` | `0x14D3944` | `0xD84414` | `0x10195FD54` | `0x17520B8` |
| `var_lr` | `0x14D3958` | `0xD84428` | `0x10195FD68` | `0x17520CC` |
| `var_ud` | `0x14D3966` | `0xD84436` | `0x10195FD76` | `0x17520DA` |

The decompilers occasionally render the UTF-16 `param` or `pv` pointers as
short `p` strings. The full byte searches and the contiguous clusters above
establish the complete keys.

## Web reconstruction and regression coverage

The Web source now uses semantic metadata/count/index/parameter/entry/ref names,
retains the native raw-owner interval, documents sequential dual publication,
and statically checks the 48-byte/28-byte entry stride selected by pointer size.
The unit test covers a malformed disabled hole, outer-element spring
coefficients, nested dynamic-state overwrite, independent owners, initialized
entry fields, cross collisions, equal keys, empty keys, and last-write-wins
sparse references.

Verification completed on 2026-08-15:

- Emscripten syntax compilation of the motionplayer unit-test translation unit:
  passed (only the pre-existing `_tss` literal-operator warning).
- CMake preset `Web Debug Build`: passed through final `index.html` link and
  shell preallocation synchronization (only pre-existing compiler/linker
  warnings).
- All four recovery IDBs were saved after local-variable renames, lifecycle/data
  flow comments, and builder/raw-append bookmarks were added.

## 2026-08-16 nested accessor / hint addendum

Fresh decompilation of all four builders and their shared vec3 helper recovered the source-level ncbind
owners omitted by the earlier semantic reconstruction: loop-wide root accessor, retained outer row source plus
metadata accessor, direct-temporary param accessor, and a helper-local copied-Variant accessor for every
`op/p/pv` vec3. The six `param/op/p/pv/ofs/baseLayer` hints are shared with Chain; `var_lr/var_ud` are shared
with Chain and Clamp; vec3 x/y are shared with shape-anchor resolution while z is helper-only. The portable
builder now uses typed getters and preserves the existing raw-owner/append/publication boundary. Full address,
cleanup and regression tables are in
`analysis/motionplayer_bust_builder_nested_ncb_accessor_vec3_hint_four_binary_2026-08-16.md`.
