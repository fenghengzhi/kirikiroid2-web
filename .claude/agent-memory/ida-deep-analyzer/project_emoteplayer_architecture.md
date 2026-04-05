---
name: EmotePlayer internal architecture
description: Deep analysis of EmotePlayer C++ implementation — object hierarchy, Player struct layout, method delegation patterns, and key binary-vs-local divergences
type: project
---

EmotePlayer is a thin 24-byte NCB shell delegating everything to a 1496-byte Player object via EmoteObject (40 bytes).

Key architecture: D3DEmotePlayer(24b) → EmoteObject(40b) → Player(0x5D8=1496b)

**Why:** The local code uses a flat shared_ptr<EmotePlayerRuntime> which is architecturally wrong vs the binary's 3-layer delegation.

**How to apply:**
- setVariable is a 9-case type dispatch (not simple map write) — Player+1384 hashmap → controller type → deque at offsets 256/336/416/576/656/736
- progress (0x530A5C→0x67D01C) is a full physics engine: 6 controller types + wind simulator + bust/hair/parts spring physics
- setScale multiplies baseScale(this+40) * userScale(this+44) — local code stores single _scale
- contains uses 3 collision types (circle/rect/quad at 0x690DF0), not AABB
- startWind creates a 0x61C-byte wind simulator object, not simple value storage
- setOuterForce dispatches to bust/h(air)/parts via string comparison — "h" not "hair"
- getScale/getRot/getColor are hardcoded stubs returning 1.0/0.0/0 in the binary
- Player field offsets documented in analysis/EmotePlayer_Internal_Implementation.md
- 62 functions renamed in IDA on 2026-04-05
