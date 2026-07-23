---
name: sub6C7440-preamble-propset-contract
description: Corrected Player source descriptor topology at +656/+676/+716 and the exact 0x6C1B70 ResourceManager.loadSource dispatch bridge
metadata:
  type: project
---

# 0x6C7440/0x6C4E28 source preamble — corrected 2026-07-23

The old note misclassified Player+676/+716 as intermediate work Layers and
Player+656 as a buffer Layer. Fresh `Player_ctor@0x6CED30`, factory comparison,
and `0x6C1B70` caller disassembly prove:

- Player+656 is a second owning Variant for the same ResourceManager dispatch.
- Player+676 is a persistent Dictionary descriptor.
- Player+716 is a persistent Dictionary used as the descriptor's numeric color
  object; ctor performs `descriptor.color = colors`.
- Player+992 is the third ResourceManager owner used by canonical/random calls.

Each render item writes `key`, `src`, and `blendMode` onto the descriptor and
indices 0..3 onto the color Dictionary with `TJS_MEMBERENSURE`. `0x6C1B70` then
dispatches `ResourceManager.loadSource(sourceObject, descriptor)` and receives a
baked Layer. The ResourceManager's inherited SourceCache owns its persistent
`bufLayer`. The old limitation “bufLayer is used only by SourceCache's
low-blend 1/2 bake” is false: `0x6C7440` buffered submit also reads that same
Layer from the RM owner, performs setSize + copy + ancestor mask, and then passes
it as the operateRect source.

Local Player now owns these two persistent Dictionaries in the matching field
order, and all production 0x6C1B70 call sites mutate and dispatch through them.
The previous “not modeled / stop-and-report” conclusion is obsolete.
