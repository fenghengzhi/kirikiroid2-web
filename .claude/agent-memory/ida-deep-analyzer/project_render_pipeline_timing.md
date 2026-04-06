---
name: Render pipeline timing and Player_calcBounds
description: Critical discovery - Player_calcBounds runs AFTER updateLayers in progress(), not during draw(). sourceWidth/Height are 0 for nodeType=0 nodes. Full call sequence and 3 coordinate systems documented.
type: project
---

Complete progress() call sequence (0x6D2A98 Player_progressCompat):
1. Player_progress_inner (time advance)
2. Player_updateLayers (state update + vertex computation via sub_6BC4F0)
3. **Player_calcBounds (0x6C3D04)** — computes bbox from vertices → node+1888
4. Player_dispatchEvents

Player_calcBounds was MISSING from our analysis of updateLayers — it runs as a separate step AFTER updateLayers.

Critical: sourceWidth/Height (node+232/240) are ALWAYS 0 for nodeType=0 nodes. Only Player_evaluateCameraNodes writes them (for nodeType=10 only). This means vertices (node+1856) from sub_6BC4F0 collapse to a single point for type=0 nodes.

Three independent coordinate/size systems in rendering:
1. node+232/240 (sourceWidth/Height) → dst vertices via sub_6BC4F0 → ZERO for type=0
2. node+1888..1900 (bbox) → clip rect via Player_calcBounds → degenerates to point if vertices=point
3. loadSource width/height → affineCopy srcW/srcH → actual texture pixel size (non-zero)

node+1936 is a POINTER to Shape AABB (4 floats), set by sub_6BDCC0 inheritance chain. If no nodeType=7 ancestor, remains null → default empty bbox (1,1,-1,-1) → no clip applied.

RenderNode struct is 432+ bytes, completely different from Node struct. Key fields documented in analysis/Player_Draw_Full_RenderPath.md.

**Why:** Needed to understand why our web port renders nothing despite correct vertex computation code.
**How to apply:** The real rendering likely uses D3D or SLA path, not the Layer direct path. OR sourceWidth/Height needs to be set from a source we haven't found yet. Check which draw path the game's TJS scripts actually invoke.
