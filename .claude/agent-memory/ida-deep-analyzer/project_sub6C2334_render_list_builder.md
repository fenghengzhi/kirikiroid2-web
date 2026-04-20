---
name: sub_6C2334 render-list builder
description: Player::updateLayers outer loop — builds RenderNode list from MotionNode tree; visibility gate is nodeType-based (0x1441/0x1449), NOT stencilType
type: project
---

sub_6C2334 @ 0x6C2334 (6KB, recursive, 3 self-calls). Called from sub_6D5164 (sort+dispatch) and sub_6D3AA8.

**What it writes on MotionNode (v288)**:
- +1944 (BYTE, "was-drawn-this-frame"): cleared to 0 at loop top (0x6C31C4), set to 1 after node enqueued (0x6C2744, 0x6C32D8)
- +1904 (QWORD, RenderNode slot pointer): stores freshly-allocated 0x1B0u RenderNode

**What it reads from MotionNode**:
- +52 stencilType: read only (0x6C2A88, 0x6C3618), propagated into RenderNode+244
- +1960 drawFlag: read only (0x6C328C, 0x6C272C, 0x6C2AAC, 0x6C361C, 0x6C25D0)
- +28 nodeType: gate-key
- +1092 completionType (on v6=Player): switches mask 0x1441↔0x1449, 0x1089↔0x1091

**What it does NOT write**:
- +52 stencilType: NEVER written. IDA insn_query for STR #52 within 0x6C2334..0x6C3B04 → 0 matches
- +1960 drawFlag: NEVER written by this function (only read as pre-existing state from sub_6BD8DC)

**Its own visibility gate (line 759, 0x6C32C0)**:
```
mask = completionType ? 0x1449 : 0x1441    // bits: {0,6,10,12} or {0,3,6,10,12}
if ((1 << nodeType) & mask) == 0: continue
```
Contrast with sub_6BD8DC drawFlag gate: 0x1801/0x1809 = bits {0,11,12} or {0,3,11,12}.

**Paradox resolution for m2logo back_white (stencilType=0, nodeType=0)**:
sub_6C2334 adds the node to the render list based on nodeType alone; stencilType=0 does NOT block it here. The "stencilType==0 ⇒ drawFlag=0 ⇒ black screen" chain only affects the +1960-flag-consumer codepath (sub_6BD8DC's output), which is a DIFFERENT rendering path from the one sub_6C2334 feeds. Both paths coexist and render different node categories.

Callees (none write +52 or +1960): sub_6C1678, sub_6C3B04, sub_6C3C04, sub_698188, sub_6996E8, sub_6637BC, sub_56C694, sub_6F3424, sub_A0BAF4/E48C/F5E0/F778.
