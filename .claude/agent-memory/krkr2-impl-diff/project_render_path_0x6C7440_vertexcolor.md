---
name: render-path-0x6C7440-vertexcolor
description: 0x6C7440 render-execute draw primitives carry vertex POSITIONS + single blend mode only; NO per-vertex/4-corner color → falsifies M9 phase-D per-vertex-color boundary justification
metadata:
  type: project
---

# Render execute draw path — per-vertex-color absence (fresh decompile 2026-06-03)

`sub_6C7440` @0x6C7440 (Player render execute, ~62KB) — the draw path that consumes the
findSource texHandle / bufLayer source. Fresh-decompiled this session.

## Draw primitives (all via iTJSDispatch2 method dispatch, UTF-16LE keys)
- `operateRect` (9 args), `operateMesh` (11), `operateBezierPatch` (11),
  `affineCopy` (14), `meshCopy` (10), `bezierPatchCopy` (10), `fillRect` (4),
  `drawMeshFrame`/`drawBezierPatchFrame`/`drawBezierPatchMeshFrame`, `drawLine`,
  `bezierPatchCopy`. Source from `player+656`(SourceCache).bufLayer.

## KEY EVIDENCE — no per-vertex color
- `sub_6C715C` @0x6C715C builds the mesh/bezier vertex array fed to operateMesh
  (node+344) / operateBezierPatch (node+400): it appends ONLY (x,y) point pairs
  (variant type 5, 20-byte stride) = vertex POSITIONS + a constant offset (a2).
  NO color/RGBA appended anywhere.
- The draws pass a SINGLE blend-op mode `v48` (from `node+48 & 0xF` switch →
  14/15/16/17/2) and a single source layer. No 4-corner color array.
- node+100..115 (the 4 RGBA colorBytes quads damped by anchor 0x6C0528) are NOT
  referenced in 0x6C7440 at all (grep "139 + 100" empty). Color, if applied, is
  baked into the bufLayer precompose UPSTREAM (single-tint), not per-vertex.

## Verdict on M9 phase-D boundary (CORRECTS m9-source-subsystem-verdict caveat)
- ResourceManager.h:18-36 justification "the binary recombines the 4-corner
  gradient as per-vertex vertex colors in its GPU draw" is **FALSIFIED for the
  primary draw path 0x6C7440** — that path applies a single precomposited
  tint + single blend mode, never a per-vertex color attribute.
- **How to apply:** do NOT cite "binary uses per-vertex GPU color" as the platform
  boundary reason. The defensible boundary (if any) is single-tint texture caching
  by (name,color); the per-vertex-color claim should be removed/rewritten. If a
  4-corner gradient is ever needed it would have to come from a DIFFERENT path not
  found in 0x6C7440 — none observed this session.
