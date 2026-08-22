# D3DEmotePlayer dead Player-property facades four-binary audit (2026-08-16)

## Scope

The portable D3DEmotePlayer declaration contained an unregistered block of
inline properties copied from Motion.Player / Motion.EmotePlayer:

```text
completionType, chara, motion, motionKey, maskMode, outline, priorDraw,
frameLastTime, frameLoopTime, loopTime
```

Together the block contributed eighteen C++ getter/setter methods. None was in
the already restored 54-member D3DEmotePlayer script table. This audit checks
all ten names against every reference binary before removing the copied facade.

## Fresh complete-registrar check

| target | D3DEmotePlayer registrar | member count | ten queried names |
|---|---:|---:|---:|
| Android arm64 | `0x52E8E4` | 54 | 0 hits |
| Android armv7 | `0x494078` | 54 | 0 hits |
| iOS arm64 | `0x100232278` | 54 | 0 hits |
| iOS armv7 | `0x230F46` | 54 | 0 hits |

The registrars were freshly decompiled rather than inferred from the portable
table. Their property sequence remains `module, visible, smoothing,
meshDivisionRatio, queing, hairScale, partsScale, bustScale, ... animating,
modified`; none of the copied Player properties is present.

## UTF-16LE literals and xref ownership

Ordinary string search returned no results because these are wide literals, so
each name was searched as explicit UTF-16LE bytes. Android shares one literal
per name; iOS generally emits separate Player and Primary copies:

| name | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `completionType` | `0x14D3E38` | `0xD847EE` | `0x10195CA7A`, `0x10196041A` | `0x174EDDE`, `0x175277E` |
| `chara` | `0x14D3DB4` | `0xD8474E` | `0x10195C1C0`, `0x10196022E` | `0x174E524`, `0x1752592` |
| `motion` | `0x14D3DC0` | `0xD8475A` | `0x10195B406`, `0x10196023A` | `0x174D76A`, `0x175259E` |
| `motionKey` | `0x14D3E56` | `0xD8480C` | `0x10195CA56`, `0x101960438` | `0x174EDBA`, `0x175279C` |
| `maskMode` | `0x14BE7EE` | `0xD76666` | `0x10195CB9E`, `0x10196045C`, `0x10196FB60` | `0x174EF02`, `0x17527C0`, `0x1761F0C` |
| `outline` | `0x14D3E7A` | `0xD84830` | `0x10195CB7C`, `0x101960492` | `0x174EEE0`, `0x17527F6` |
| `priorDraw` | `0x14D3E8A` | `0xD84840` | `0x10195C670`, `0x1019604A2` | `0x174E9D4`, `0x1752806` |
| `frameLastTime` | `0x14D3E9E` | `0xD84854` | `0x10195CCB8`, `0x1019604B6` | `0x174F01C`, `0x175281A` |
| `frameLoopTime` | `0x14D3EBA` | `0xD84870` | `0x10195CCD4`, `0x1019604D2` | `0x174F038`, `0x1752836` |
| `loopTime` | `0x14D3ED6` | `0xD8488C` | `0x10195C810`, `0x1019604EE` | `0x174EB74`, `0x1752852` |

Fresh complete xref enumeration assigns the literals as follows:

- all ten names are published by Motion.Player and/or Motion.EmotePlayer;
- `chara`, `motion`, `priorDraw`, and `loopTime` also have their expected
  Player load/evaluation data-path users;
- `maskMode` additionally belongs to the separate D3DEmoteModule registrar;
- **no xref for any of the ten names belongs to the D3DEmotePlayer registrar**.

This distinction matters for `maskMode`: a nearby D3D module property and the
two `MaskMode*` D3DEmotePlayer constants do not imply a D3DEmotePlayer
`maskMode` property.

## Local source graph

The eighteen inline methods had no local D3DEmotePlayer caller. They only
forwarded through the shell's unchecked primary-object chain into the embedded
Player, duplicating APIs already owned by Motion.Player or Motion.EmotePlayer.
Because they were inline and unused, the binaries cannot provide standalone
function symbols; the complete registrar/literal xref audit supplies the
observable class-boundary evidence, while the zero-caller source scan supplies
the portable source-graph evidence.

The block was removed from `D3DEmotePlayer`. Real registered properties and
methods are unchanged. The existing real-class-object regression now checks
all ten names with `TJS_MEMBERMUSTEXIST`, requires
`TJS_E_MEMBERNOTFOUND`, and verifies that failure leaves the result Variant
untouched.

