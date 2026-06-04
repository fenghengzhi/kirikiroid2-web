---
name: Player +1092 render gate is TJS "preview" NOT "completionType"
description: CORRECTION — Player byte +1092 (0x444) is the backing field of TJS property "preview", not "completionType". TJS "completionType" is the +1144 int. The +1092 render/timeline gate behavior is unchanged; only the property-name attribution was wrong (Player NCB-table off-by-one). +1144 int = completionType.
type: project
---

# Player +1092 (0x444) — backing field of TJS property `"preview"` (NOT `"completionType"`)

## CORRECTION (2026-06-04, off-by-one fix on Player_ncb_registerMembers @0x6d6c80)

The earlier version of this memory attributed byte +1092 to the TJS property
`"completionType"`. That attribution came from the IDA symbol `Player_getCompletionType`
@0x6D9634 (which reads +1092). **That symbol name was an off-by-one mislabel** in the
Player registration table (every property getter/setter symbol was shifted to a neighbor's
name). Byte-verified ground truth from the NCB `addMember` bindings:

- member **`"preview"`** (UTF-16LE @0x14d9986, full string) → struct v33 → getter @0x6D9634
  reads `*(u8*)(this+1092)`, setter @0x6D963C writes `*(u8&1)(this+1092)`.
  → these funcs are now renamed `Player_getPreview` / `Player_setPreview`.
- member **`"completionType"`** (UTF-16LE @0x14d73f8, full string) → struct v30 → getter
  @0x6D9624 reads `*(uint*)(this+1144)`, setter @0x6D962C writes `*(int*)(this+1144)`.
  → renamed `Player_getCompletionType` / `Player_setCompletionType`.

So: **TJS `completionType` is the +1144 int field. TJS `preview` is the +1092 byte bool.**

## What is STILL correct (field-+1092 behavior, just relabeled)

All the byte-verified FIELD facts below are about offset +1092 and remain valid. The only
thing that changed is the TJS name of that field (preview, not completionType):

- +1092 is a 1-byte bool, ctor default 0 (STRB WZR [X19,#0x444] @0x6CF0A4).
- Render-build gate sub_6B3C78 @0x6B43A4: `CBZ` confirms `+1092 != 0 ⇒ stencilType &= ~4`.
- Timeline bitmask gates pick `0x1809` (include type-3 node) when `+1092 != 0`, else `0x1801`,
  across initNodeTimeline/advance/rewind/filter/updateLayers/sub_6D1528.
- sub_6C2334 build-render-items and Player_calcBounds skip type==3/4 handling per +1092.
- Full read-site list (0x6B43A4, 0x6B6704, 0x6B72F8, 0x6B7FF4, 0x6BA2D4, 0x6BB314, the
  updateLayers loops, 0x6C31C8/0x6C337C/0x6C38A0, 0x6C3EF4/0x6C4030, 0x6D180C) all read +1092.

**Semantic re-reading:** the field behaves as a "render this sub-player independently /
force-decompose" flag — which fits TJS name `preview` (preview-mode independent draw) better
than the previously assumed `completionType`.

## How to apply

- Local web port: the TJS property exposed as `preview` is the +1092 bool render-gate
  described above (getter/setter on the 1-byte flag, plus the stencil/bitmask side effects).
- The TJS property `completionType` is a SEPARATE +1144 int field (plain int get/set, no known
  render-gate side effects in the catalogued read sites).
- Any local code that wired `completionType` to the +1092 render-gate semantics is mis-named;
  the render gate belongs to `preview`. Re-check NCB binding before trusting any Player
  property getter/setter IDA symbol — the whole table was off-by-one (see
  [[project_player_ncb_table_off_by_one]]).
