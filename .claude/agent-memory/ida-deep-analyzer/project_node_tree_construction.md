---
name: Node tree construction pipeline
description: Complete call chain for PSB node tree building - Player_buildNodeTree(0x6B51F0), recursive builder(0x6B4A6C), field initializer(0x6B3C78), and critical finding that sourceWidth/sourceHeight are NOT set during construction
type: project
---

Node tree construction chain:
- Player_buildNodeTree (0x6B51F0): reads PSB content["layer"], calls recursive builder, then links deflector mask layers
- Player_buildNodeTree_recursive (0x6B4A6C): for each layer element, allocates 2632-byte node in deque, calls requireLayerId (node+16/20), calls Player_initNodeFields, reads "children" and recurses
- Player_initNodeFields (0x6B3C78): fills label, type, coordinate, inheritMask, frameList, transformOrder[4], stencilType, meshTransform, and type-specific fields. Does NOT fill sourceWidth/sourceHeight
- Player_resetAndReleaseNodes (0x6B56F8): cleanup before rebuild, calls releaseLayerId for all nodes
- Player_evaluateCameraNodes (0x6C0528): only for type==10 nodes, reads source icon width/height into node+232/240 at render time
- Player_evaluateTimeline (0x699AE4): interpolates frame-based animation properties (angle, scale, slant, offset, color, opacity) but NOT sourceWidth/sourceHeight
- Player_initVariables (0x6CD750): reads PSB "variable" array into Player+1296 deque

Critical finding: sourceWidth (node+232) and sourceHeight (node+240) as doubles are ONLY written by Player_evaluateCameraNodes for type==10 nodes. For other node types, these offsets are never explicitly written in the entire 0x690000-0x700000 range.

**Why:** Needed to understand whether web port should set sourceWidth/sourceHeight during buildNodeTree or dynamically.
**How to apply:** Do NOT set sourceWidth/sourceHeight during node construction. They are runtime values read from source icon dict, only for camera nodes.
