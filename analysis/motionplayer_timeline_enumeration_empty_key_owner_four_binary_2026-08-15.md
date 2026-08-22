# Timeline enumeration null-label lookup and owner flow — four-reference reconstruction

Date: 2026-08-15

This vertical re-audits the count/index/flags query cluster against the four
current binaries.  It specifically replaces the overly broad old conclusion
that every invalid `getPlayingTimelineFlagsAt` index returns zero.

## Address map

| Role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| public count main | `0x530A8C` | `0x494DBA` | `0x100233200` | `0x231E5E` |
| public get main label | `0x530AA8` | `0x494DC8` | `0x10023321C` | `0x231E70` |
| public count diff | `0x530AB4` | `0x494DD6` | `0x100233228` | `0x231E7E` |
| public get diff label | `0x530AD0` | `0x494DE4` | `0x100233244` | `0x231E90` |
| public count playing | `0x530ADC` | `0x494DF2` | `0x100233250` | `0x231E9E` |
| public get playing label | `0x530AF8` | `0x494E00` | `0x10023326C` | `0x231EB0` |
| public get playing flags | `0x530B04` | `0x494E0E` | `0x100233278` | `0x231EBE` |
| Engine get main label | `0x672064` | `0x55B45C` | `0x1001AED80` | `0x1AE554` |
| Engine get diff label | `0x6720CC` | `0x55B4A4` | `0x1001AEDE4` | `0x1AE5A0` |
| Engine get playing label | `0x672134` | `0x55B4EC` | `0x1001AEE48` | `0x1AE5EC` |
| Engine get playing flags | `0x67219C` | `0x55B534` | `0x1001AEEAC` | `0x1AE638` |
| string-from-literal helper | `0xA12178` | `0x761F4C` | `0x10025F590` | `0x26061E` |

The public label/flags bodies resolve the Engine from the primary Emote object
and forward the 32-bit index.  The three count bodies are inlined: each reads
the corresponding vector begin/end pointers and divides the byte distance by
one pointer-sized `ttstr` element.

## Count and index ABI

The generated public surface receives `tjs_int`, while the Engine helpers use
`tjs_uint32`.  A negative script-facing index is therefore transferred as the
same 32 bits and interpreted as a large unsigned number by the Engine.  Each
label helper performs an unsigned `index >= size` check before any element load,
so negative inputs never address memory before `begin`.

The count result is the vector element count narrowed to the 32-bit return
register.  There is no saturation, exception, or separate cached count.  The
source-level `static_cast<tjs_int>(vector.size())` preserves that operation.

## Label return ownership

On a hit, each helper loads the selected `ttstr` backing pointer into the hidden
return object and increments the backing object's reference count.  The returned
label therefore remains valid independently of the vector element's later
lifetime.

On a miss, each helper passes the platform's global empty literal to the normal
string constructor helper.  Fresh decompilation of all four helpers yields the
same first branch:

```cpp
StringBody *makeString(const char16_t *s) {
    if (s == nullptr || s[0] == 0)
        return nullptr;
    // allocate body and copy payload
}
```

Thus the miss result is a null-backed `ttstr`.  Local `ttstr()` is the exact
source representation; changing it to a specially allocated empty buffer would
alter both hash and ordering boundaries in this port.

## Flags lookup data flow

The four targets share the following observable sequence:

```cpp
int getPlayingFlags(uint32_t index) {
    ttstr label = getPlayingLabel(index);    // owning CopyRef or null
    auto found = timelineStates.find(label); // HM3, non-inserting
    int result = found == timelineStates.end() ? 0 : found->second.flags;
    // local label releases after the find/result read
    return result;
}
```

There is no `label.empty()` branch between the label helper and HM3.  For a
null-backed label the native hash path uses hash zero without a mutable Hint
slot.  Therefore the exact edge matrix is:

| Playing index/label | HM3 node | Result |
| --- | --- | --- |
| invalid index -> null label | absent | `0` |
| invalid index -> null label | null-label node present | that node's flags |
| valid null-label vector entry | absent | `0` |
| valid null-label vector entry | null-label node present | that node's flags |
| valid non-empty label | absent | `0` |
| valid non-empty label | present | that node's flags |

Every miss is non-inserting.  No state node, bucket, or label backing object is
created by the flags query itself.

## Platform/container differences

- Main/diff/playing vector elements are 8 bytes on both arm64 targets and 4
  bytes on both armv7 targets.
- Android uses its old-libstdc++ unordered-table family; iOS uses libc++.
  Bucket/node layout and mapped-value flags offsets differ, but lookup order and
  the null-key behavior agree.
- arm64 returns labels through the hidden `X8` result pointer.  armv7 carries
  the hidden result in `R0` and shifts `this`/index to `R1`/`R2`.
- Android arm64 uses an exclusive atomic increment loop for a non-null returned
  backing string; the 32-bit targets use their corresponding refcount sequence.

## Android-arm64 IDA ownership defect

IDA currently assigns the real Engine flags body at `0x67219C..0x672334` as a
tail chunk of the D3D forwarder at `0x530B04`.  Direct disassembly of the Engine
range is coherent and agrees with the other three clean decompilations, but a
whole-function decompile can include the wrapper's prefix.  A bounded code-item
redefinition restored all instructions after testing the boundary; the exposed
MCP operations cannot detach the tail-chunk ownership itself.  The database is
annotated accordingly instead of presenting the contaminated pseudocode as a
real combined source function.

## Port comparison and regression

The implementation already preserved the correct vector arithmetic, unsigned
bounds checks, return CopyRef, null-backed fallback, and unconditional HM3
lookup.  No semantic C++ rewrite was needed.  Source comments now fix the two
easy-to-regress facts: the miss is a null-backed `ttstr`, and flags must still
look it up.

The differential unit case now also proves that:

- a missing empty key returns zero without growing HM3;
- after an empty-key node is installed, a negative/out-of-range index returns
  its stored flags;
- a valid playing entry with an empty label reaches the same node; and
- ordinary missing and present non-empty labels retain their old behavior.

Validation after the source/test update:

- the Emscripten syntax-only test translation-unit check passes (one pre-existing
  deprecated literal-operator spelling warning);
- the three-step `Web Debug Build` rebuild reaches the final `index.html` link;
  and
- the targeted whitespace/error diff check passes.
