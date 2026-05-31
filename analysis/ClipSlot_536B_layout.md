# ClipSlot 536B 权威字段布局表 (libkrkr2.so)

> 来源：parseFrame `0x6926B4` / mergeFrameContent `0x692AB0` / evaluateTimeline `0x699AE4` / resetFrameSlot `0x69260C`。
> 每个偏移都有反编译证据地址。slot 大小经 `ida_funcs` 确认 = 536 字节（mergeFrameContent 函数体 0x692904..0x6932F4，slot[0]=MotionNode+320，slot[1]=MotionNode+856；evaluateTimeline 全程用 `536 * v5` 作为 stride，`v5 = *(int*)(node+1392)` 是 active slot index 0/1）。

## 0. 结论速查（任务第 2 点）

| 问题 | 答案 | 证据 |
|---|---|---|
| slot+0 = frameIndex (int) | **是**。`*(_DWORD *)a1 = a3`（a3=frameIndex） | parseFrame 0x6926ec |
| slot+8 = time (double) | **是**。`*(double *)(a1+8) = propGet("time")` | parseFrame 0x6927e0 |
| mask 偏移 | **slot+20** (int32)。`*(_DWORD*)(a1+20) = content["mask"]` | parseFrame 0x6928e8；merge 读 `v3[5]`(=+20) @0x692dc0 |
| typeZeroFlag (invisible) 偏移 | **slot+24** (1 字节 bool)。type==0 → `*(_BYTE*)(a1+24)=1` 且 return；否则 =0 | parseFrame 0x692828/0x69280c；merge 早退 `v5=*(u8*)(result+24); if(v5) return` @0x692aec |
| interpFlag 偏移 | **slot+25** (1 字节 bool)。frame type: 2→0, 3→1（type1 不写，保留 reset 的 0） | parseFrame 0x692834；merge 大量 `*((_BYTE*)v3+25)` gate @0x693098/0x6930cc/0x6932c4 |
| mergedFlag 偏移 | **slot+26** (1 字节 bool)。merge 入口无条件 `*(_BYTE*)(result+26)=1`；reset 清 0 | merge 0x692af0；reset 0x692624 |
| clipStartTime 偏移 (eval 的 +328) | **slot+328** (double)。reset: `slot+328 = slot+320`（即 = curve-vector begin 指针的低 8 字节被复用为 double） | eval 0x699c90 (`v17=*(double*)(v16+328)`)；reset 0x6926a4 |
| 4 个 packed color dword | **slot+72/76/80/84** (4×u32 BGRA)。reset 默认 `0xFF808080`×4 | merge 0x692c7c (`*(int32x4_t*)(v3+18)=vdupq_n_s32(0xFF808080)`，v3+18 = byte+72) |
| opacity 偏移 | **slot+88** (int/u8 实际取低字节)。reset 默认 255 | merge 0x692c8c (`v3[22]=255`，v3[22]=+88)；opa 写 0x693468 |
| blendMode 偏移 | **slot+44** (int)。reset 默认 16 | merge 0x692c90 (`v3[11]=16`，v3[11]=+44)；bm 写 0x692f48 |

---

## 1. 完整字段表（按偏移升序）

说明：
- "访问形式" 列里 `v3[N]` 是 u32 数组下标（merge 的 v3 是 `unsigned int*`），字节偏移 = N*4；`(double*)v3+N` 字节偏移 = N*8；`(_BYTE*)v3+N` 即字节偏移 N。
- "门控" 给出读写该字段的条件（mask 位或 flag）。
- 证据列给函数缩写 + 指令地址：PF=parseFrame, MF=mergeFrameContent, EV=evaluateTimeline, RS=resetFrameSlot。

| 偏移 | 类型 | 语义 | 门控 | 证据 |
|---|---|---|---|---|
| +0 | int32 | **frameIndex** | 无（PF 总写）；RS 清 0 | PF `*(_DWORD*)a1=a3` 0x6926ec；RS 0x692620 |
| +8 | double | **time** (帧时间戳) | 无（PF 总写）；RS 清 0 | PF 0x6927e0；RS 0x69262c |
| +16 | int32 (u32) | **ti** (texture/tile index)，merge 写 `v3[4]` | interpFlag(+25 区块)内 `(*(u8*)v3+23 & 4)` ⇒ 读 "ti"；注意 +23 是 +20(mask) 的最高字节，即 `mask & 0x4000000`。RS 清 0 (随 +8 的 OWORD) | MF 0x6930c8 (`v3[4]=propGetInt("ti")`)；门控 0x6930a0；RS `*(_QWORD*)(a1+16)=0` 0x69262c |
| +20 | int32 | **mask** (内容字段位掩码) | 无（PF 总写）；RS 清 0 (随 +0 的 DWORD... 实为 +20 单独?) — 注意 RS 只显式清 +0；mask 由 PF 每帧覆写 | PF `*(_DWORD*)(a1+20)=propGetInt("mask")` 0x6928e8；MF 读 `v28=v3[5]` 0x692dc0 |
| +24 | byte (bool) | **typeZeroFlag / invisible** (frame type==0) | PF: type0→1 早退；type!=0→0。MF/EV 早退门控。RS 清 0 | PF 0x692828/0x69280c；MF 0x692aec；EV 用 +344 不用 +24（见下） |
| +25 | byte (bool) | **interpFlag** (frame type==3 ⇒ 1, type==2 ⇒ 0) | PF type 分派；MF 多处 gate（ti/curves/cp）。RS 清 0 | PF 0x692834；MF 0x693098 等；RS 0x692628 (WORD@+24 清 +24/+25) |
| +26 | byte (bool) | **mergedFlag** (mergeFrameContent 已执行) | MF 入口无条件置 1；RS 清 0 | MF 0x692af0；RS 0x692624 |
| +36 | ptr (tTJSVariant*) | **icon** variant（src/icon 合并对象，引用计数） | nodeType gate `(1<<a2)&0x1849` ⇒ 写。RS Release 并清 0 | MF `*v24=v135[0]` (v24=`(double*)(v3+7)`=+56? 见注) — **实为 +56**，见下条修正；RS `*(a1+36)` Release 0x69261c |
| +36 | ptr | **(修正) src variant**：merge `*(_QWORD*)(v3+9)=v150`，v3+9 字节=+36 | nodeType gate 0x1849。RS Release(+36) | MF 0x692ce4 (`*(_QWORD*)(v3+9)=v150`)；RS 0x69261c |
| +44 | int32 | **blendMode (bm)** | reset=16；`mask&0x20600` 区块内 `mask&0x20000` ⇒ propGetInt("bm")。RS 清 0 | MF reset 0x692c90；bm 0x692f48；RS 0x692644 |
| +56 | double | **ox** (origin x) | `mask&1` ⇒ propGet("ox") | MF `*((double*)v3+7)=propGet("ox")` 0x692de8；RS OWORD@+56 清 0 0x69265c |
| +56 | ptr (overlap) | **icon variant**：`v24=(double*)(v3+7); *v24=v135[0]` | nodeType gate 0x1849（与 ox 共址，互斥使用：icon 仅 type-gated 节点，ox 仅 mask&1） | MF 0x692da8；RS 0x69265c |
| +64 | double | **oy** (origin y) | `mask&1` ⇒ propGet("oy") | MF `*((double*)v3+8)=oy` 0x692e10；RS OWORD@+56 清 0 |
| +72 | u32 | **color[0]** packed BGRA ch0 | reset `0xFF808080`；`mask&0x200` 内 case1 `sub_6637BC(...,0)`；其余 case fill。RS 间接(随 +72 OWORD) | MF reset 0x692c7c；ch0 0x6942ac；RS 0x692658 (OWORD@+72) |
| +76 | u32 | **color[1]** packed BGRA ch1 | 同上 (`v3[19]`) | MF 0x6942c4；RS 0x692658 |
| +80 | u32 | **color[2]** packed BGRA ch2 | 同上 (`v3[20]`) | MF 0x6942dc |
| +84 | u32 | **color[3]** packed BGRA ch3 | 同上 (`v3[21]`) | MF 0x6942f4 |
| +88 | int32 (低字节=opacity) | **opacity (opa)** | reset=255；`mask&0x400` 且 `(blend&0xF0)!=0` 路径 ⇒ propGetInt("opa")。RS 清 0 | MF reset 0x692c8c；opa 写 `v3[22]=v42` 0x693468；RS 0x692648 |
| +96 | double | **coord[0]** (group coord x) | `mask&2` ⇒ propGetIndexDouble(0) | MF `*((double*)v3+12)` 0x692ebc；RS OWORD@+96 0x692660 |
| +104 | double | **coord[1]** (coord y) | `mask&2` ⇒ idx1 | MF `*((double*)v3+13)` 0x692ed4 |
| +112 | double | **coord[2]** (coord z) | `mask&2` ⇒ idx2 | MF `*((double*)v3+14)` 0x692eec；RS `*(_QWORD*)(a1+112)=0` 0x69264c |
| +120 | byte (bool) | **fx** (flip x) | `mask&0xC` (具体 0x4) ⇒ propGetBool("fx") | MF `*((_BYTE*)v3+120)` 0x692f94；EV 复制到 node+1507 0x699d78；RS WORD@+120 0x692650 |
| +121 | byte (bool) | **fy** (flip y) | `mask&0xC` (0x8) ⇒ propGetBool("fy") | MF `*((_BYTE*)v3+121)` 0x692fc0；EV→node+1508 0x699d80 |
| +128 | double | **angle** | `mask&0x10` ⇒ propGet("angle") | MF `*((double*)v3+16)=angle` 0x692fec；EV 插值(180 wrap)→node+1536 0x699e38；RS OWORD@+128 0x692664 |
| +136 | double | **zx** (scale x / zoom x) | `mask&0x60` (0x20) ⇒ propGet("zx") | MF `*((double*)v3+17)` 0x693018；EV→node+1544 0x699e9c |
| +144 | double | **zy** (scale y / zoom y) | `mask&0x60` (0x40) ⇒ propGet("zy") | MF `*((double*)v3+18)` 0x693040；EV→node+1552 0x699ef4 |
| +152 | double | **sx** (skew/shear x) | `mask&0x180` (0x80) ⇒ propGet("sx") | MF `*((double*)v3+19)` 0x69306c；EV→node+1560 0x699f54；RS OWORD@+144 0x692664 |
| +160 | double | **sy** (skew/shear y) | `mask&0x180` (0x100) ⇒ propGet("sy") | MF `*((double*)v3+20)` 0x693090；EV→node+1568 0x699fd0；RS `*(_QWORD*)(a1+160)=0` 0x692654 |
| +168 | curve block (20B) | **ccc** (angle easing curve) | interpFlag && `mask&0x800` ⇒ `sub_A0FB64(v3+42, ...)` (v3+42=+168)。RS `sub_A0F790(a1+168)` | MF 0x69312c；RS 0x692668 |
| +188 | curve block (20B) | **occ** (opacity easing curve) | interpFlag && `mask&0x8000` ⇒ `sub_A0FB64(v3+47,...)` (+188) | MF 0x69318c；RS 0x692670 |
| +208 | curve block (20B) | **acc** (angle curve, EV 用 +208 = `v22+208`) | interpFlag && `mask&0x1000` ⇒ `sub_A0FB64(v3+52,...)` (+208)；EV angle 插值用 `sub_69A754(v22+208,...)` gate `slot+544` | MF 0x6931ec；EV 0x699dfc；RS 0x692678 |
| +228 | curve block (20B) | **zcc** (zoom curve, EV 用 +228) | interpFlag && `mask&0x2000` ⇒ `sub_A0FB64(v3+57,...)` (+228)；EV zx/zy 插值用 `+228`/`+228` gate `slot+564` | MF 0x69324c；EV 0x699e7c/0x699ed8；RS 0x692680 |
| +248 | curve block (20B) | **scc** (skew curve, EV 用 +248) | interpFlag && `mask&0x4000` ⇒ `sub_A0FB64(v3+62,...)` (+248)；EV sx/sy 插值用 `+248` gate `slot+584` | MF 0x6932ac；EV 0x699f34/0x699f90；RS 0x692688 |
| +268 | curve block (20B) | **cp** (control point / position easing curve) | interpFlag && `(opacity&1)` (`*((_BYTE*)v3+22)&1`)... 实为 `*v3+25 && *((_BYTE*)v3+22)&1` ⇒ `sub_A0FB64(v3+67,...)` (+268)。EV pos 插值 sub_69A4D4(v46+42,...) v46+42=+168? 见注 | MF 0x693314；门控 0x6932c4；RS 0x692690 |
| +288 | ptr (tTJSVariant*) | **act** (action variant) | PF: `mask&0x40000` ⇒ propGet("act")。RS（act 在 +296 block? 见注）；PF Release+set | PF `*(_QWORD*)(a1+288)=v28[0]` 0x69293c |
| +296 | curve block (20B) | **(reset 第7个曲线块)** — 经 `sub_A0F790(a1+296)` 释放；与 +288 act ptr 相邻，act 占 +288..+295(8B)，曲线块从 +296 | RS only (无 MF 直接写名) | RS 0x692698 |
| +320 | ptr / qword | **mesh/bezier vector begin** (位置 curve-vector 三元组 begin)；EV type==1(`node+2000`) 时参与 sub_69AC4C 位置曲线插值 | reset: `v3=*(a1+320)` 然后写回 +328 | RS 0x69269c (读)；EV `*(v57+320)` 0x69a06c |
| +328 | double / qword | **clipStartTime** (eval 的 +328)。同时是 curve-vector 三元组的第二槽 | EV: `v17=*(double*)(slot+328)` 作为本 slot 起始时间；reset 写 = +320 值 | EV 0x699c90；RS `*(a1+328)=v3` 0x6926a4 |
| +336 | u32 | **timeModulo** (eval 的 +336，时间取模周期) | EV: `v18=*(u32)(slot+336)`；非 0 ⇒ `v19 = modulo(a3-startT, v18)` | EV 0x699c94 |
| +344 | byte (bool) | **slotActiveFlag / hasContent** (eval 入口门控) | EV: `if(!*(u8)(slot[active]+344))` 进入插值；reset 清 0 (随 +344) | EV 0x699b38；RS `*(a1+344)=0` 0x6926a0 |
| +345 | byte (bool) | **slotSecondaryFlag** (crossfade 双 slot 标志) | EV: `if(!*(u8)(slot+345) || other_slot+344)` ⇒ 走 copy 分支否则插值 | EV 0x699b60 |
| +360 | byte (bool) | **motion.docmpl** (motion 子对象 docmpl) | `mask&0x80000` motion 块，`motionMask&4` ⇒ propGetBool("docmpl")；reset 0 | MF `*((_BYTE*)v3+360)=...` 0x693988/0x693a1c |
| +344 (qword) | qword=0x100000000 | **motion.flags+dt** (motion 块 `*((_QWORD*)v3+43)=0x100000000`，v3+43=+344) | motion 块 reset。注意与 +344 bool 重叠语义——motion 子对象复用区块 | MF 0x69398c |
| +384 | byte (bool) | **model.loop** | `mask&0x1000000` model 块 ⇒ propGetBool("loop") | MF `*((_BYTE*)v3+384)` 0x693bc4 |
| +392 | u32 | **type4 color/channel ch (eval copy +392)** | EV copy 分支 `*(node+100)=*(slot+392)` | EV 0x699bc8 |
| +396 | u32 | type4 channel +396 → node+104 | EV | EV 0x699bd0 |
| +400 | u32 | type4 channel +400 → node+108 | EV | EV 0x699bd8 |
| +404 | u32 | type4 channel +404 → node+112 | EV | EV 0x699be0 |
| +408 | u32 | **opacity-result channel (eval +408)** → node+1576 | EV copy 分支 `*(node+1576)=*(slot+408)` | EV 0x699bf0 |
| +416 | qword/double | **transform-result block (eval +416..+432)** → node+1512/1520/1528 | EV copy 分支 | EV 0x699bb0/0x699bb8/0x699bc0 |
| +424 | double | type4 interp channel (eval +424) → node+2224 | EV type4 插值 `slot+424` | EV 0x69a104 |
| +432 | double | type4 +432 → node+2232 | EV | EV 0x69a154 |
| +440 | byte | **fx-result (eval +440)** → node+1507 | EV copy 分支 `*(node+1507)=*(slot+440)` | EV 0x699b78 |
| +441 | byte | **fy-result (eval +441)** → node+1508 | EV copy 分支 | EV 0x699b80 |
| +440 | double (overlap) | type4 +440 → node+2240 | EV type4 插值 | EV 0x69a1a8 |
| +448 | qword | **angle-result (eval +448)** → node+1536 | EV copy 分支 `*(node+1536)=*(slot+448)`；插值分支单独算 | EV 0x699b88 |
| +456 | qword | **zx-result (eval +456)** → node+1544 | EV copy 分支；type4 +456→node+2256 | EV 0x699b90 / 0x69a24c |
| +464 | qword | **zy-result (eval +464)** → node+1552；type4→node+2264 | EV | EV 0x699b98 / 0x69a29c |
| +472 | qword | **sx-result (eval +472)** → node+1560；type4→node+2272 | EV | EV 0x699ba0 / 0x69a2f0 |
| +480 | qword | **sy-result (eval +480)** → node+1568；type4→node+2280 | EV | EV 0x699ba8 / 0x69a340 |
| +488 | double | type4 +488 → node+2288 (type4 第8通道) | EV type4 | EV 0x69a394 |
| +496 | double | **type5 channel (eval +496)** → node+2368 | EV type5 (copy: slot+816；插值: +496) | EV 0x699d44 / 0x69a43c |
| +528 | double | **type10 channel (eval +528)** → node+2432 | EV type10 (copy: slot+848；插值: +528) | EV 0x699d30 / 0x69a3e8 |
| +544 | u32 | **angle easing-enabled flag** (EV gate) | EV: `if(*(u32)(slot[active]+544))` ⇒ angle 用 sub_69A754(slot+208) easing；否则线性 | EV 0x699de8 |
| +564 | u32 | **zoom easing-enabled flag** (EV gate, zx/zy 共用) | EV: `if(*(u32)(slot+564))` ⇒ zx/zy 用 sub_69A754(slot+228) | EV 0x699e68/0x699ec4 |
| +584 | u32 | **skew easing-enabled flag** (EV gate, sx/sy 共用) | EV: `if(*(u32)(slot+584))` ⇒ sx/sy 用 sub_69A754(slot+248) | EV 0x699f20/0x699f7c |
| +640 | (block) | **type1/cluster-G easing source** (eval `node+2000==1` ⇒ sub_6996E8(node+2024, slot+640)) | EV 0x699c08 | EV |
| +696 (v3+91=+364?) | ptr | motion.dtgt variant (`v3+91` 字节=+364) | motion 块 `motionMask&0x10` | MF 0x693a98 (`v73=(v3+91)`)；注意 v3+91 是 u32 下标 ⇒ +364 |
| +744 (v3+97=+388) | u32 | model.dt (`v3[97]`=+388) | model 块 | MF 0x693be8 |
| +784 (v3+98=+392 overlap) | ptr | model.dtgt variant (`*((_QWORD*)v3+49)`=+392) | model 块 | MF 0x693c38 |
| ~+416 (v3[104]=+416) | u32 | prt.trigger (`v3[104]`=+416) | `mask&0x100000` prt 块 `prtMask&1` | MF 0x693d74 |
| ~+424.. (double v3+53..61) | double×N | prt fmin/fmax/vmin/vmax/amin/amax/zmin/zmax/range (`(double*)v3+53..61`=+424..+488) | prt 块各 prtMask 位 | MF 0x693d9c..0x693ecc |
| +496 (double v3+62=+496) | double | camera.fov (`(double*)v3+62`=+496) | `mask&0x200000` camera 块 | MF 0x693fa4 |
| +504 (v3+63=+504) | ptr | camera.target variant | camera 块 | MF 0x693ff4 |
| +1032 (v3+129=+516) | ptr | anchor.target variant (`v3+129`=+516) | `mask&0x800000` anchor 块 | MF 0x694104 |
| +528 (double v3+66=+528) | double | feedback.timespan (`(double*)v3+66`=+528) | `mask&0x8000000` feedback 块 | MF 0x6941e4 |

> ⚠ **重叠注意**：merge 阶段的 prt/camera/anchor/feedback 子对象字段与 eval 阶段读的 type4/5/10 result 字段、easing flag 字段在数值偏移上**部分重合**（如 +416、+424、+496、+528）。这是因为 motion/model/prt/camera 等子对象与 transform-result/type-channel 是**联合体（union）式复用**：一个 slot 对一个 node 只可能是其中一种类型，所以这些字段共用同一段内存。对齐本地结构时应按 `union` 处理或按 nodeType 分支只实例化对应字段。

---

## 2. 三函数职责与 slot 访问入口

### parseFrame `0x6926B4` (slot=a1)
写 slot 的"帧头"字段：
```
resetFrameSlot(a1)                       // 清空整个 slot
slot+0  = frameIndex (a3)                // 0x6926ec
slot+8  = frame["time"] (double)         // 0x6927e0
type = frame["type"]:                    // 0x692800
  0 → slot+24 = 1 (invisible), return    // 0x692828
  2 → slot+25 = 0 (interpFlag)           // 0x692830
  3 → slot+25 = 1                        // 0x69281c
  (1 → slot+24=0, slot+25 保留 reset 的 0)// 0x69280c
slot+20 = content["mask"] (int32)        // 0x6928e8
if (mask & 0x40000) slot+288 = frame["act"] (variant) // 0x69293c
```
**不调用 mergeFrameContent**（注释已确认）。

### mergeFrameContent `0x692AB0` (slot=v3, nodeType=a2, contentDispatch=a3)
```
v5 = slot+24; slot+26 = 1 (mergedFlag)   // 0x692af0
if (v5 /*invisible*/) return             // 0x692af4
// --- reset 默认值 ---
slot+72..84 = 0xFF808080 ×4 (color)      // 0x692c7c
slot+88 = 255 (opacity)                  // 0x692c8c
slot+44 = 16 (blendMode)                 // 0x692c90
// --- src/icon: nodeType gate (1<<nodeType)&0x1849 ---
if gate: slot+36=src, slot+56=icon       // 0x692ce4 / 0x692da8
mask = slot+20 (=v3[5])
mask&1     → ox/oy (+56/+64)             // 0x692de8/0x692e10
mask&2     → coord[0..2] (+96/+104/+112) // 0x692ebc..
mask&0x20600 group:
  mask&0x20000 → bm (+44)                // 0x692f48
  mask&0x200   → color (+72×4 或 -1 fill when blend&0xF0==0) // switch v136
  mask&0x400 (且 blend&0xF0) → opa (+88) // 0x693468
mask&0x1FC:
  mask&0xC   → fx/fy (+120/+121)         // 0x692f94/0x692fc0
  mask&0x10  → angle (+128)              // 0x692fec
  mask&0x60  → zx/zy (+136/+144)         // 0x693018/0x693040
  mask&0x180 → sx/sy (+152/+160)         // 0x69306c/0x693090
interpFlag(slot+25) gates:
  (mask&0x4000000 via byte+23) → ti (+16)// 0x6930c8
  mask&0xF800 → ccc/occ/acc/zcc/scc (+168/+188/+208/+228/+248) // sub_A0FB64
  (interp && opacity&1) → cp (+268)      // 0x693314
mask&0x2000000 → mesh/bezierPatch (+320 vector) // 0x69376c..
// --- 子对象 ---
mask&0x80000   → motion  (+344/+360/...) // 0x693980..
mask&0x1000000 → model   (+384/+392/...) // 0x693bc4..
mask&0x100000  → prt     (+416/+424..)   // 0x693d74..
mask&0x200000  → camera  (+496/+504)     // 0x693fa4..
mask&0x800000  → anchor  (+516)          // 0x694104
mask&0x8000000 → feedback(+528)          // 0x6941e4
```

### evaluateTimeline `0x699AE4` (node=a1, force=a2, time=a3)
```
v5 = *(int)(node+1392)                    // active slot index 0/1
slot[active] = node + 320 + 536*v5
result = force || node+44
if (slot[active]+344 == 0):              // 0x699b38 hasContent gate
  if (slot+345==0 || slot[other]+344):   // 0x699b60 单 slot copy 分支
    // 直接拷贝 active slot 的 result 字段到 node
    node+1507 = slot+440 (fx)            // 0x699b78
    node+1508 = slot+441 (fy)
    node+1536 = slot+448 (angle)
    node+1544 = slot+456 (zx)
    node+1552 = slot+464 (zy)
    node+1560 = slot+472 (sx)
    node+1568 = slot+480 (sy)
    node+1512/1520/1528 = slot+416/424/432 (pos)
    node+100/104/108/112 = slot+392/396/400/404 (color ch)
    node+1576 = slot+408 (opacity)
    switch(node+28) type 4/5/10: copy slot+? to node+2224../2368/2432
  else:  // 双 slot crossfade 插值
    startT = slot[active]+328             // clipStartTime
    mod    = slot[active]+336
    ratio v3 = (time-startT)/(slot[other]+328 - startT)  // 0x699ccc
    angle: slot+128 → 180°shortest-path lerp → node+1536 // gate slot+544
    zx:    slot+136 → lerp → node+1544                   // gate slot+564
    zy:    slot+144 → node+1552                           // gate slot+564
    sx:    slot+152 → node+1560                           // gate slot+584
    sy:    slot+160 → node+1568                           // gate slot+584
    pos:   sub_69A4D4(slot+168?, ...) → node+1512..
    color: slot+72..84 → node+100..112
    opa:   slot+88 → round → node+1576
    type4/5/10 channel lerp (slot+424.. / +496 / +528)
```

---

## 3. 未定项 / 需进一步确认

1. **+36 vs +56 的 src/icon 归属**：merge 在 nodeType-gated 块里既写 `*(_QWORD*)(v3+9)`(=+36，src variant) 又写 `*v24`(v24=`(double*)(v3+7)`=+56，icon variant)。但 +56 同时是 `mask&1` 的 ox。两者**互斥**（icon 路径只对 type-gated 节点，ox 路径只对 `mask&1`），但本地结构需确认这是 union 还是 IDA 把两个不同字段合到了同一偏移。建议 `get_bytes` dump 一个真实 type-gated slot 验证 +36/+56 的实际占用。

2. **+288 act 与 +296 curve block 的边界**：act 是 8B 指针 (+288..+295)，reset 的第 7 个 `sub_A0F790(a1+296)` 从 +296 开始。但 reset 的曲线块列表是 +168/+188/+208/+228/+248/+268/+296（注意 +268 之后跳到 +296，缺 +288，因为 +288 被 act ptr 占用）。需确认 +296 这个 20B 块对应哪个命名曲线（可能是 cp 的第二半或独立 mesh-curve）。

3. **+320/+328 三元组的真实类型**：reset 把 +320 读出写到 +328，+344 清 0，构成 libstdc++ vector 的 (begin, end, cap) 三指针三元组？还是 (clipStartTime double @ +328 + timeModulo @ +336 + flag @ +344)？eval 把 +328 当 double 读、+336 当 u32 读、+344 当 byte 读，而 reset 把 +320→+328 当指针拷贝。**强烈怀疑 +320..+344 是 mesh/bezier 的 std::vector 控制块 (begin@+320, end@+328, cap@+336) 与 eval 复用同一内存当 startTime/modulo**。merge 的 bezier 循环正是操作 `*((_QWORD*)v3+40)`(=+320) / `*((_QWORD*)v3+41)`(=+328) 作为 vector begin/end（0x693860）。⇒ **+320/+328/+336 在"有 bezier mesh"时是 vector<float[2]> 控制块；在 timeline eval 时被当 startTime/modulo 复用**。这是关键 union，本地必须按此对齐。

4. **type4/5/10 result 字段 (+392..+528) 与 prt/camera/anchor/feedback merge 字段重叠**：已在表中标注为 union。需按 nodeType 拓扑确认每个偏移在哪种 type 下是哪个语义。

5. **easing-enabled flag (+544/+564/+584)** 的写入点不在本三函数内（merge 不写、reset 不写、eval 只读）。它们应在某个 curve-block 解析函数（sub_A0FB64 内部？）中根据曲线是否存在而置位。需反编译 `sub_A0FB64` (0xA0FB64) 确认。

6. **mesh/bezier 字段 v3+82/v3+84 (=+328/+336)** 与 #3 一致：merge bezier 循环 `v46=(float**)(v3+82)`(=+328)、`v3+84`(=+336) 作为 vector 的 end/cap 指针。进一步坐实 +320..+336 是 std::vector 控制块。
