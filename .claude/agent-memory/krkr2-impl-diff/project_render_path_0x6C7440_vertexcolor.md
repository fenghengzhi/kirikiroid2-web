---
name: render-path-0x6C7440-vertexcolor
description: Corrected 4-corner color flow through Player descriptor/color Dictionaries, SourceCache std::list identity, and sub_6A7518 software/GPU branches
metadata:
  type: project
---

# 0x6C7440 4-corner color flow — corrected 2026-07-23

Anchor/render-item colors reach the four numeric slots of the persistent
Player+716 color Dictionary. Player+676 is the persistent descriptor Dictionary
and ctor `0x6CED30` stores `descriptor.color = colors`; Player+656 is the second
owner of the ResourceManager dispatch. The older interpretation of these fields
as work Layers was wrong.

For every item, `0x6C7440`/`0x6C4E28` write descriptor key/src/blendMode and color
indices 0..3. `0x6C1B70` calls the ResourceManager dispatch's inherited
`loadSource(source, descriptor)` method. SourceCache uses
`std::list<Entry>` with strict `(full Variant key, ttstr src, blendMode)` identity;
color is mutable state. Same-color hit returns directly; a mismatch rebakes the
same Layer and copies the Entry to the list front before erasing the old node.

`sub_6A7518` is the color consumer. It returns for all `0xFF808080` or bitwise
white. In software it performs per-pixel bilinear BGRA multiplication with
literal width-1/height-1 spans; RGB divisor is 128 when high blend bits are set,
otherwise 255, while alpha divisor is always 255. In the GPU branch it only
queries the PrivateMotionGLL native instance and discards the result. The prior
claim that the GPU branch bakes pixels and the fallback submits a GL operation
was directionally wrong.

Draw primitives still carry positions/blend/opacity rather than per-vertex
colors. This is original behavior, not a license to cache by color or invent a
platform-boundary implementation.
