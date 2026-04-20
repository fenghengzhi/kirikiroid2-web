---
name: Player+1092 completionType semantics
description: Player class byte-0x444 is a 1-byte bool TJS property "completionType"; gates whether mask/bitmask logic treats child type=3/4 nodes as visible. Binary-verified direction and all read/write sites.
type: project
---

# Player + 1092 (offset 0x444) — `completionType`

## Field facts (all byte-verified)
- **Type**: 1 byte (bool), NOT a packed bitfield. Each of +1092..+1100 is an independent 1-byte flag.
- **TJS property name**: `"completionType"` (UTF-16LE string at `aCompletiontype`, registered in `Player_ncb_registerMembers` @ 0x6D70A0→0x6D70F4).
- **Default**: 0, set by `Player_ctor` @ 0x6CED30 (STRB WZR, [X19,#0x444] at 0x6CF0A4).
- **Getter**: `Player_getCompletionType` @ 0x6D9634 (single LDRB W0,[X0,#0x444]).
- **Setter**: `Player_setCompletionType` @ 0x6D963C (AND W8, W1, #1; STRB W8,[X0,#0x444]).

## Write points (complete list from byte-pattern scan of `?? 12 11 39` = STRB [Xn,#0x444])
| Addr | Function | Write |
|---|---|---|
| 0x6CF0A4 | Player_ctor | `STRB WZR` — ctor default 0 |
| 0x6D9640 | Player_setCompletionType | `STRB W8` (TJS setter, value & 1) |

(0x670C78 is a STRB `[X0,#0x444]` inside `sub_670AFC` but that function is a stride-12 zero-fill loop on an unrelated object; not a Player write.)

## Read points (LDRB `[Xn,#0x444]` with confirmed Player base)
| Addr | Function | Behavior when flag != 0 | Behavior when flag == 0 |
|---|---|---|---|
| **0x6B43A4** | **sub_6B3C78 (build RenderNode, type==3 branch)** | **stencilType &= ~4 (strip composite bit)** | leave stencilType as-is |
| 0x6B6704 | Player_initNodeTimeline_guess | bitmask = 0x1809 (include type 3) | bitmask = 0x1801 |
| 0x6B72F8 | Player_advanceRootAndNodes_guess | bitmask = 0x1809 | bitmask = 0x1801 |
| 0x6B7FF4 | Player_advanceNodeFrames_guess | bitmask = 0x1809 | bitmask = 0x1801 |
| 0x6BA2D4 | Player_rewindRootAndNodes_guess | bitmask = 0x1809 | bitmask = 0x1801 |
| 0x6BB314 | sub_6BB300 (filter predicate) | bitmask = 6153 (0x1809) | bitmask = 6145 (0x1801) |
| 0x6BC034 / 0x6BC900 / 0x6BCEC8 / 0x6BD01C / 0x6BD970 / 0x6BE104 / 0x6BEDF4 / 0x6BF11C | updateLayers phase loops | same pattern | same pattern |
| 0x6C31C8 | sub_6C2334 (build render items) | **skip** node.type==4 particle handling | enter particle handling |
| 0x6C337C | sub_6C2334 | bitmask = W12 (expanded) | bitmask = 0x441 |
| 0x6C38A0 | sub_6C2334 | branch into alloc/push of independent render item (via node+0x770) | fall through to normal alloc path |
| 0x6C3EF4 | Player_calcBounds | **skip** node.type==4 bounds | enter type==4 bounds |
| 0x6C4030 | Player_calcBounds | **skip** node.type==3 bounds | enter type==3 bounds |
| 0x6D180C | sub_6D1528 | same 0x1809/0x1801 bitmask pattern | — |
| 0x6D9634 | Player_getCompletionType | (TJS getter) | (TJS getter) |

`0x1809` vs `0x1801` difference = bit 3 set. Bit 3 represents node type=3 (motion sub-node). So the pattern across advance/rewind/filter/update loops is: **`completionType != 0` → allow type=3 nodes through the type-bitmask filter**.

## sub_6B3C78 @ 0x6B43A0 direction — JUDGMENT

Raw bytes at 0x6B43A4..0x6B43B3:
```
6B43A4  88 12 51 39        LDRB   W8, [X20,#0x444]   ; W8 = player.completionType
6B43A8  62 00 00 34        CBZ    W8, loc_6B43B4     ; if (W8 == 0) goto 0x6B43B4
6B43AC  08 79 00 12        AND    W8, W0, #0xFFFFFFFB ; stencilType & ~4
6B43B0  68 36 00 B9        STR    W8, [X19,#0x34]     ; node+0x34 = stencilType
6B43B4  ...                STR    WZR, [X19,#0x7D0]   ; (unconditional next insn)
```

CBZ = Compare and Branch if Zero. When `W8 == 0` (completionType == 0), **skip** the AND+STR block.
Therefore: **stencilType &= ~4 happens iff `player.completionType != 0`.**

IDA's decompiler renders this as:
```c
if ( *((_BYTE *)a1 + 1092) )
    *(_DWORD *)(a2 + 52) = v24 & 0xFFFFFFFB;
```
which is correct and equivalent.

## Semantic interpretation

`completionType` appears to be a "sub-player finished/completion replay" flag. When set, the child Motion sub-node:
1. Becomes visible to timeline advance/rewind/bounds passes (bitmask 0x1801→0x1809 adds bit 3 for type==3),
2. Has stencil composite bit-2 stripped at tree-build (sub_6B3C78 @ 0x6B43B0), i.e. it is forced to render as an *independent* draw item rather than composited through parent alpha-mask,
3. Takes a different render-item allocation path in sub_6C2334 (0x6C38A0 CBZ branch).

This is consistent with a "post-completion / finished-state sub-player" semantics where child motion continues drawing independently after parent completion.

## Verdict on prior-doc conflict

- `analysis/Player_Class_Layout_libkrkr2so.md:214` → **correct** (+1092 = completionType, 1-byte bool).
- `project_player_class_layout.md:26` → **correct in spirit** (+1092..+1100 are 9 independent 1-byte flags, first is completionType). The phrase "packed bool flags" refers to *adjacent* booleans, not C bitfield packing.
- `project_render_executor.md:53` Branch A description `!player+1092 && node+28==3` → **ambiguous notation**. The actual sub_6C2334 branch gates are multi-step; the `completionType != 0` path at 0x6C38A0 allocates a rendered item via `node+0x770`, while `completionType == 0` falls through. Reader should not interpret that line as a precise C expression for the branch condition.

**How to apply:** Local web port must implement `player.completionType` as a 1-byte bool setter-accessible via TJS. The gate at sub_6B3C78 @ 0x6B43B0 is `completionType != 0 ⇒ stencilType &= ~4` (not the opposite). The bitmask-choice gates across advance/rewind/bounds/build pick `0x1809` when set, `0x1801` when clear.
