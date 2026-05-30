---
name: cluster-f-node-path-key
description: Player+24 map and HM3 are PATH-keyed (/top/.../leaf) not flat-label; buildNodePathKey @0x6B5C1C generator + port mapping + HM3 defer
metadata:
  type: project
---

Cluster F / audit M5 (resolved 2026-05-30): the Player+24 node-index map is a
node-PATH map, NOT a flat-label map.

**Key generator** Player_buildNodePathKey @0x6B5C1C:
- usercall: X0=player, W1=nodeIndex, X8=ttstr_out.
- Walks parentIndex chain (node+36) leaf→root. Per node: segment = ttstr("/")+
  label(node+0) via sub_A0CC68; accumulated = segment + accumulated via
  sub_A1359C (ancestor PREPENDED). Loop `while(a2)` — stops when parentIndex==0
  (synthetic root index 0 NOT emitted). Result = "/topLabel/.../selfLabel".
  Inserts even for empty label (bare "/" segment).
- Port: motion::detail::buildNodePathKeyLike_0x6B5C1C(nodes, nodeIndex) in
  RuntimeSupport.cpp/.h.

**Consumers of Player+24 path map (all match RAW string verbatim, no transform):**
- Insert: Player_nodePathMap_lowerBoundInsert @0x6B50B8, called @0x6B4CE4 in
  buildNodeTree_recursive @0x6B4A6C. value = deque-index. Port: NodeTree.cpp
  walkTree → player._nodeLabelMap[buildNodePathKey]=index (unconditional).
- Find: Player_nodePathMap_find @0x6F2228.
- getLayerMotion/getLayerGetter: sub_6B5AD8 @0x6B5B14 — TJS name is a path.
- stencil mask resolve: @0x6B5454 — raw stencilCompositeMaskLayerList element is
  a path. Port: NodeTree.cpp post-pass.
- dtgt resolves (angleMode=4 @0x6BE7B4, particle trigger=4 @0x6BF048): motionDtgt
  is a path. Port: findNodeByLabel(_nodeLabelMap, dtgt) in PlayerUpdateChildMotion
  /PlayerUpdateParticles.
- requireLayerId @PlayerResource.cpp, hitTestLayer @PlayerLayerQuery.cpp.

**getLayerNames divergence:** binary sub_6D1018→sub_6B601C @0x6B601C does NOT
iterate the path map — walks Player+200 node deque with visitor descending into
type3 child players / type4 particle arrays. Port iterates path-map keys instead
(now emits paths). Re-port to sub_6B601C is separate, out of keying scope.

**HM3 (Player+1184) populate DEFERRED:** key CONFIRMED = node-path ttstr (same
generator). resetMotionState loop3 @0x6B2DF8: buildNodePathKey @0x6B2E08 →
Player_HM3_upsert_perNodeLayerState @0x6F2674 → Player_HM3_initValueFromNode
@0x699510 (688-byte node→V snapshot). Port has NO resetMotionState equivalent and
no caller, and HM3_initValueFromNode is unported, so HM3 map stays empty. When
ported, key via buildNodePathKeyLike_0x6B5C1C so HM3 + Player+24 share one key gen.

**Why:** binary is authoritative; flat-label keying was an architectural
divergence (CLAUDE.md reject functional-equiv).
**How to apply:** if touching getLayer*/dtgt/stencil/HM3 keying, the map is
path-keyed; do NOT transform lookup strings (binary feeds raw path strings).
Logo (yuzulogo, type0-only, no dup names) differential stays 0-mismatch.
m2logo fails on pre-existing frame-count spec mismatch (100 vs 93), unrelated.
