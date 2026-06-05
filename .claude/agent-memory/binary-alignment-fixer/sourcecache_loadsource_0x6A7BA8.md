---
name: sourcecache-loadsource-0x6A7BA8
description: loadSource@0x6A7BA8 cache model — (key,src,blendMode) match, color is mutable not a key; one node per (key,blendMode)
metadata:
  type: project
---

`Motion_ResourceManager_loadSource @0x6A7BA8` (SourceCache) intrusive doubly-linked
list head = `this+72` (`a1+72`), LRU counter at `this+60` (`+=v65` only on MISS).

Node layout (verified from match loop 0x6a8004 + clone 0x6EAC60, 0x58 bytes):
- [0]=next [1]=prev (`__int64*`)
- +16 (`v27+2`)  = key ttstr (matched via sub_A10428)
- +36            = Layer variant (the baked source Layer; output to caller a4 via sub_A0FB64)
- +56 (`v27[7]`) = src ttstr (matched: ptr-eq OR len@+60 eq + sub_9B1ED0 UTF-16 cmp)
- +64 (`+16 dw`) = blendMode (matched `== v60`)
- +68..+80 (`+17..+20 dw`) = color[4] — **NOT a match key; mutable**

Match key = (key, src, blendMode). color excluded.
HIT + color UNCHANGED (0x6a80d4 false) → return node as-is, no rebuild.
HIT + color CHANGED → write new color +68..+80 (0x6a80d8) → re-bake source bitmap
via sub_6A6BE0 (drawLayer copyRect/fillRect/operateRect/adjustGamma = per-pixel
color bake @sub_6A7518) → CLONE node to list head (sub_6EAC60+sub_146359C) →
unhook+delete old node (sub_14635B8 + operator delete). Net: ONE mutable node per
(key,blendMode), color overwrites in place.
MISS → evictLRU + create Layer via global Layer.CreateNew → sub_6A6BE0 bake →
insert at head, `this+60 += v65`.

Local SourceCache.cpp aligned 2026-06-06 (C-2/P3): findEntry/const overload/
ensureEntry match (key|resolvedKey, blendMode) only; color change → update
packedColors + reset backingBitmap/sourceObject/sourceTexture → re-bake. Fast-return
in loadRenderSourceByName/...TextureByName gated on `packedColors==packedColors`.
Was: 3-tuple key (key,blend,color) → N independent immutable entries per color.

NOT a platform boundary: local HAS re-bake capability (cloneBitmap32 +
applyPackedCornerTintLike_0x6A7518). The std::list node clone+delete is container
ABI detail, not required — same Entry mutated in place is the correct source-level
equivalent.

Residual modeling note (pre-existing, not from this fix): binary compares src
(+56) as an independent 3rd predicate; local folds src into resolvedKey. Same
partition for normal key→src maps; not re-modeled (orthogonal to C-2, no divergence
evidence under logo fixtures).

Verification: web debug build green; SourceCache.cpp compiles clean. motion_playback
logo differential (m2logo/yuzulogo) is wasmtime+lldb lane — wasmtime NOT installed
locally, could not run in-session. Non-regression argued statically: no-color-change
path (logos use neutral color, oracle shows no per-item color tokens) is
byte-identical to old behavior; only divergent path is the color-change fix itself.
