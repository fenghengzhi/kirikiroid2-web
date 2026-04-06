---
name: sub_692AB0 PSB key mapping (verified)
description: Complete byte-verified UTF-16LE key-to-field mapping for sub_692AB0 (readContentState), correcting IDA mislabeled ASCII strings
type: project
---

sub_692AB0 reads PSB animation state from "content" dictionary using iTJSDispatch2::PropGet with UTF-16LE keys. ALL keys are UTF-16LE, none are ASCII. IDA mislabeled many as ASCII single-char.

**Why:** Previous local code used wrong PSB keys ("z" instead of "zx", "s" instead of "sx") causing properties to always get default values.

**How to apply:** When updating PlayerInternal.h psbDictionaryNumber() calls, use the corrected key names.

### Verified key mapping (mask & field):

| Mask     | Key (UTF-16LE) | IDA showed | Field (slot offset)     |
|----------|---------------|------------|-------------------------|
| 0x01     | `"src"`       | L"src"     | image source (via sub_529524) |
| 0x01     | `"icon"`      | "i"        | icon (PropGet sub-dict)  |
| 0x01     | `"coord"`     | "c"        | coord (PropGet sub-dict) |
| 0x01     | `"ox"`        | L"ox"      | slot+7 (originX)         |
| 0x01     | `"oy"`        | L"oy"      | slot+8 (originY)         |
| 0x20000  | `"bm"`        | "b"        | slot[11] (blendMode)     |
| 0x200    | `"color"`     | L"color"   | slot+18..21 (RGBA)       |
| 0x400    | `"opa"`       | L"opa"     | slot[22] (opacity)       |
| 0x04     | `"fx"`        | L"fx"      | byte 120 (flipX)         |
| 0x08     | `"fy"`        | L"fy"      | byte 121 (flipY)         |
| 0x10     | `"angle"`     | L"angle"   | slot+16 (angle)          |
| 0x20     | `"zx"`        | "z"        | slot+17 (scaleX)         |
| 0x40     | `"zy"`        | L"zy"      | slot+18 (scaleY)         |
| 0x80     | `"sx"`        | "s"        | slot+19 (slantX)         |
| 0x100    | `"sy"`        | L"sy"      | slot+20 (slantY)         |
| bit2 of byte23 | `"ti"` | "t"        | slot[4] (timeline)       |
| 0x800    | `"ccc"`       | L"ccc"     | ccc bezier curve         |
| 0x8000   | `"occ"`       | L"occ"     | occ bezier curve         |
| 0x1000   | `"acc"`       | L"acc"     | acc bezier curve         |
| 0x2000   | `"zcc"`       | L"zcc"     | zcc bezier curve         |
| 0x4000   | `"scc"`       | L"scc"     | scc bezier curve         |
| byte22&1 | `"cp"`        | L"cp"      | cp bezier curve          |
| 0x2000000| `"mesh"`      | L"mesh"    | mesh sub-dict            |
|          | `"obj"`       | "o"        | mesh object array        |
|          | `"mcc"`       | "m"        | mesh curve control       |
|          | `"cc"`        | L"cc"      | mesh cc                  |
|          | `"bp"`        | L"bp"      | bezier patch             |
|          | `"bezierPatch"` | L"bezierPatch" | bezier patch fallback |
| 0x80000  | `"motion"`    | L"motion"  | motion sub-dict          |
|          | `"mask"`      | L"mask"    | motion.mask              |
|          | `"flags"`     | L"flags"   | motion.flags             |
|          | `"dt"`        | L"dt"      | motion.dt                |
|          | `"docmpl"`    | L"docmpl"  | motion.docmpl            |
|          | `"dofst"`     | L"dofst"   | motion.dofst             |
|          | `"dtgt"`      | L"dtgt"    | motion.dtgt              |
|          | `"timeOffset"`| L"timeOffset" | motion.timeOffset     |
| 0x1000000| `"model"`     | L"model"   | model sub-dict           |
| 0x100000 | `"prt"`       | L"prt"     | particle sub-dict        |
|          | `"trigger"`   | L"trigger" | prt.trigger              |
|          | `"fmin"`      | L"fmin"    | prt slot+53              |
|          | `"fmax"`      | "f"        | prt slot+54              |
|          | `"vmin"`      | L"vmin"    | prt slot+55              |
|          | `"vmax"`      | "v"        | prt slot+56              |
|          | `"amin"`      | L"amin"    | prt slot+57              |
|          | `"amax"`      | "a"        | prt slot+58              |
|          | `"zmin"`      | L"zmin"    | prt slot+59              |
|          | `"zmax"`      | "z"        | prt slot+60              |
|          | `"range"`     | L"range"   | prt slot+61              |
| 0x200000 | `"camera"`    | L"camera"  | camera sub-dict          |
|          | `"fov"`       | "f"        | camera slot+62           |
|          | `"target"`    | L"target"  | camera.target            |
| 0x800000 | `"anchor"`    | L"anchor"  | anchor sub-dict          |
| 0x8000000| `"feedback"`  | L"feedback"| feedback sub-dict        |
|          | `"timespan"`  | L"timespan"| feedback.timespan        |
