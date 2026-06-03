---
name: render-path-0x6C7440-vertexcolor
description: RESOLVED node+100 4-corner color consumer = sub_6A7518 per-pixel bilinear bake into source texture (NOT per-vertex GPU color). Draw primitives carry positions+blendMode+opacity only. Local port faithful.
metadata:
  type: project
---

# Render draw path + node+100 4-corner color consumption — RESOLVED (fresh decompile 2026-06-03, 2nd pass)

Re-decompiled this session: 0x6C7440 (top-level execute), 0x6C4E28 (per-item emit),
0x6C715C (vertex builder), 0x6C0528 (anchor color writer), 0x6C1B70 (source resolver),
0x6A7518 (THE color consumer), 0x6996E8 (mesh-pts vector copy).

## THE node+100 color consumption point — FOUND (closes the long-open question)
Chain: anchor 0x6C0528 damps + writes 4-corner colorBytes to node+100/104/108/112
  -> render-item snapshot copies them to item+168/172/176/180 (via 0x6C2334 setup;
     0x6996E8 only copies mesh/bezier point std::vectors, NOT color)
  -> 0x6C7440 @0x6c7944-0x6c7a7c writes the 4 colors as index-properties 0/1/2/3 onto
     the source-resolver object (player+716) via vtbl+56. (0x6C4E28 does same @0x6c5528.)
  -> 0x6C1B70 (source resolver) special branch reads them back from player+716 into
     v41[0..3], then calls sub_6A7518(v41, player+736_bitmap, &dstRect, (blend&0xF0)==16).
  -> **sub_6A7518 @0x6A7518 = per-pixel 4-corner BILINEAR gradient multiply baked into
     the source bitmap pixels.** Reads 4 corner colors, lerps RGBA per row (v34..v47) and
     per pixel, multiplies each texel; divisor 128 if (a4&1) else 255. GPU branch
     (hasGPUAccel) locks texture bits sub_80A820/80A840 and bakes on GPU texture; non-GPU
     fallback dispatches vtbl+200 (PrivateMotionGLL). EITHER WAY color is baked into the
     texture, never a per-vertex attribute.

## VERDICT (supersedes both prior caveats)
- M9 phase-D "binary recombines 4-corner gradient as per-vertex GPU vertex colors" is
  WRONG in mechanism but the BOUNDARY ITSELF IS GENUINE. Correct mechanism = 4-corner
  bilinear gradient baked per-pixel into the source texture (sub_6A7518), then drawn with
  positions + single blendMode + single opacity. Draw primitives (operateRect/affineCopy/
  meshCopy/bezierPatchCopy/OperateRect/AffineCopy/...) carry NO color arg — confirmed in
  BOTH 0x6C7440 and 0x6C4E28 (grep color/opacity/rgba = 0 hits; only a1+1144 stretch-type
  & a1+1148 stencil-type & neutralColor=literal-0).
- LOCAL PORT IS FAITHFUL: SourceCache caches by (key, blendMode, packedColors[4]) and
  applyPackedCornerTintLike_0x6A7518 (SourceCache.cpp:82) reproduces the bilinear per-pixel
  bake incl. 128/255 divisor. Execute draws call OperateRect/AffineCopy/MeshCopy/
  BezierPatchCopy(positions, image, srcRect, blend, opacity) — exact arg match. effectiveColor
  (unpackPackedRgba) in PlayerRenderExecute.cpp is LOG-ONLY, never passed to a draw. Good.
- So the defensible boundary statement = "single pre-tinted texture cached by
  (name,blendMode,4-corner-color); per-pixel color bake done on CPU/GPU texture upstream,
  drawn with positions+blend+opacity." NOT "per-vertex GPU color". Update ResourceManager.h
  comment wording if it still says per-vertex.
