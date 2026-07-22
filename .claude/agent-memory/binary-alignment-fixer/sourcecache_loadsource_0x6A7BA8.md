---
name: sourcecache-loadsource-0x6A7BA8
description: Exact SourceCache loadSource descriptor parsing, std::list Entry identity, same-Layer rebake, node-copy lifetime, trim, bake, and clear boundaries
metadata:
  type: project
---

# SourceCache_loadSource @0x6A7BA8

Public source signature:

```cpp
tTJSVariant loadSource(iTJSDispatch2 *source,
                       iTJSDispatch2 *descriptor);
```

Descriptor parsing:

- `key`: ordinary `PropGet`, preserve the full `tTJSVariant`; missing remains void.
- `src`: probe with `TJS_MEMBERMUSTEXIST`, then read string; missing defaults empty.
- `blendMode`: same probe/read shape; missing defaults 0.
- `color`: ordinary `PropGet`; a non-void object is probed at numeric indices 0..3,
  each missing item defaulting 0. If color itself is void, only `colors[0]` is
  written (`0xFF808080` when `blend&0xF0`, otherwise `0xFFFFFFFF`); slots 1..3
  remain genuinely uninitialized.

Source container is `std::list<Entry>`, proved by libstdc++
`_List_node_base::_M_hook/_M_unhook` and copier `0x6EAC60`, not a custom
intrusive list. Entry semantic order is Variant key, Variant Layer, ttstr src,
blendMode, raw `tjs_int colors[4]`, byteWeight.

Match identity is strict `(full Variant key, src, blendMode)`; color is not a
key. Same-color hit returns the cached Layer with no callback or promotion.
Color mismatch updates colors, calls `bake@0x6A6BE0` on the same Layer, then
`push_front(*it)` and erases the old node. Miss calls trim before Layer creation,
bakes, adds `4*width*height` to current bytes, and pushes a copy at the front.

`bake@0x6A6BE0` calls `source.drawLayer(entry.layer)`. It reads Layer width/height,
records weight, applies `tint@0x6A7518`, and only for low blend 1/2 follows the
`bufLayer.setSize/copyRect -> layer.fillRect/operateRect` path; blend 2 also calls
`adjustGamma(1,255,0 x3)`. The GPU PrivateMotionGLL gate skips this low-blend
path. `_bufLayer` is not the drawLayer target.

`tint@0x6A7518` returns for all neutral or bitwise-white colors. Software uses
literal `(width-1)/(height-1)` bilinear interpolation, RGB divisor 128 or 255,
and alpha divisor 255. The GPU branch only queries the PrivateMotionGLL native
instance and discards the result.

`trim@0x6A6B08` keeps a greedy subsequence, not necessarily a prefix.
`clearCache@0x6A8438` calls Layer dispatch `Invalidate(self)`, clears the list,
and resets current bytes.

Player ctor `0x6CED30` owns persistent descriptor/color Dictionaries with
`descriptor.color = colors`; every `0x6C1B70` caller rewrites them and invokes
the ResourceManager dispatch's `loadSource(source, descriptor)` method.
