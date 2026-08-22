# MotionPlayer node-frame mergeContent 完整 member-hint family 四参考复原（2026-08-16）

## 范围与结论

本纵切面承接 V151 的 `time/type/content/mask/act` 组和 V152 的完整 accessor owner tree，
重新以 `reference/binaries/` 四份产品代码 fresh 审计 `MotionNodeFrameSlot_mergeContent_guess`
使用的所有命名属性 hint。四端共同证明：`act` 后紧接一段连续的 53 个、每项 4 bytes 的
process-wide mutable member-hint 槽，字段从 `src` 严格排列到 `timespan`；这不是 merge 函数
私有数组，而是多个 render/query/load 函数按相同绝对身份共享的一组独立全局。

结论要点：

- 53 个槽的顺序和 4-byte stride 在四端完全一致；
- `src`、`coord`、`color`、`angle`、`mesh`、`bezierPatch`、`motion` 有 merge 之外的
  xref；其余 46 个槽只由 mergeContent 使用；
- nested motion 与 model 复用同一个 `timeOffset`、`dt`、`dtgt` 槽，camera 与 anchor
  复用同一个 `target` 槽；
- nested motion.mask 与 prt.mask 不在本组重复分配，而是复用 V151 的
  `maskMemberHint_guess`；
- `icon` 是关键例外：MEMBERMUSTEXIST probe 和随后 flags=0 String getter 都传 null hint，
  四端均不存在 icon 槽；
- load-motion 请求 Dictionary 的 `motion` PropSet 与 mergeContent 的 nested motion getter
  共享同一个 `motion` 槽。portable 侧原来的 `requestMotionMemberHint_guess` 因而是过时重复；
- portable 侧此前把 native 单一 `src/coord/color` 身份分别拆成 command/calc 两套全局，
  本轮已按四端 xref 合并。

符号已剥离，恢复名继续保留 `_guess`。绝对地址只记录在本文与 recovery IDB，不写进编译
源码注释。

## 四端主函数与 family 边界

| target | mergeContent | size | family first `src` | family last `timespan` | preceding `act` |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x68FE90` | `0x1E38` | `0x1AB5134` | `0x1AB5204` | `0x1AB5130` |
| Android armv7 | `0x56F06C` | `0xE4C` | `0x1111668` | `0x1111738` | `0x1111664` |
| iOS arm64 | `0x1000F1970` | `0x1154` | `0x101B695FC` | `0x101B696CC` | `0x101B695F8` |
| iOS armv7 | `0xEDD80` | `0x1276` | `0x187D32C` | `0x187D3FC` | `0x187D328` |

每个地址均满足 `base + index * 4`。fresh `xref_query(xref_type=data)` 对 53×4 个地址全部返回
非零结果；IDB 拆分后的 fresh global readback 在每个数据库中都精确返回 53 项、每项
`size=4`，不存在尾部 aggregate 或重叠数组。

## 53 槽精确映射

| idx | field / recovered symbol | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 0 | `srcMemberHint_guess` (`src`) | `0x1AB5134` | `0x1111668` | `0x101B695FC` | `0x187D32C` |
| 1 | `nodeFrameOxMemberHint_guess` (`ox`) | `0x1AB5138` | `0x111166C` | `0x101B69600` | `0x187D330` |
| 2 | `nodeFrameOyMemberHint_guess` (`oy`) | `0x1AB513C` | `0x1111670` | `0x101B69604` | `0x187D334` |
| 3 | `coordMemberHint_guess` (`coord`) | `0x1AB5140` | `0x1111674` | `0x101B69608` | `0x187D338` |
| 4 | `nodeFrameBmMemberHint_guess` (`bm`) | `0x1AB5144` | `0x1111678` | `0x101B6960C` | `0x187D33C` |
| 5 | `colorMemberHint_guess` (`color`) | `0x1AB5148` | `0x111167C` | `0x101B69610` | `0x187D340` |
| 6 | `nodeFrameOpaMemberHint_guess` (`opa`) | `0x1AB514C` | `0x1111680` | `0x101B69614` | `0x187D344` |
| 7 | `nodeFrameFxMemberHint_guess` (`fx`) | `0x1AB5150` | `0x1111684` | `0x101B69618` | `0x187D348` |
| 8 | `nodeFrameFyMemberHint_guess` (`fy`) | `0x1AB5154` | `0x1111688` | `0x101B6961C` | `0x187D34C` |
| 9 | `angleMemberHint_guess` (`angle`) | `0x1AB5158` | `0x111168C` | `0x101B69620` | `0x187D350` |
| 10 | `nodeFrameZxMemberHint_guess` (`zx`) | `0x1AB515C` | `0x1111690` | `0x101B69624` | `0x187D354` |
| 11 | `nodeFrameZyMemberHint_guess` (`zy`) | `0x1AB5160` | `0x1111694` | `0x101B69628` | `0x187D358` |
| 12 | `nodeFrameSxMemberHint_guess` (`sx`) | `0x1AB5164` | `0x1111698` | `0x101B6962C` | `0x187D35C` |
| 13 | `nodeFrameSyMemberHint_guess` (`sy`) | `0x1AB5168` | `0x111169C` | `0x101B69630` | `0x187D360` |
| 14 | `nodeFrameTiMemberHint_guess` (`ti`) | `0x1AB516C` | `0x11116A0` | `0x101B69634` | `0x187D364` |
| 15 | `nodeFrameCccMemberHint_guess` (`ccc`) | `0x1AB5170` | `0x11116A4` | `0x101B69638` | `0x187D368` |
| 16 | `nodeFrameOccMemberHint_guess` (`occ`) | `0x1AB5174` | `0x11116A8` | `0x101B6963C` | `0x187D36C` |
| 17 | `nodeFrameAccMemberHint_guess` (`acc`) | `0x1AB5178` | `0x11116AC` | `0x101B69640` | `0x187D370` |
| 18 | `nodeFrameZccMemberHint_guess` (`zcc`) | `0x1AB517C` | `0x11116B0` | `0x101B69644` | `0x187D374` |
| 19 | `nodeFrameSccMemberHint_guess` (`scc`) | `0x1AB5180` | `0x11116B4` | `0x101B69648` | `0x187D378` |
| 20 | `nodeFrameCpMemberHint_guess` (`cp`) | `0x1AB5184` | `0x11116B8` | `0x101B6964C` | `0x187D37C` |
| 21 | `meshMemberHint_guess` (`mesh`) | `0x1AB5188` | `0x11116BC` | `0x101B69650` | `0x187D380` |
| 22 | `nodeFrameObjMemberHint_guess` (`obj`) | `0x1AB518C` | `0x11116C0` | `0x101B69654` | `0x187D384` |
| 23 | `nodeFrameCcMemberHint_guess` (`cc`) | `0x1AB5190` | `0x11116C4` | `0x101B69658` | `0x187D388` |
| 24 | `nodeFrameMccMemberHint_guess` (`mcc`) | `0x1AB5194` | `0x11116C8` | `0x101B6965C` | `0x187D38C` |
| 25 | `nodeFrameBpMemberHint_guess` (`bp`) | `0x1AB5198` | `0x11116CC` | `0x101B69660` | `0x187D390` |
| 26 | `bezierPatchMemberHint_guess` (`bezierPatch`) | `0x1AB519C` | `0x11116D0` | `0x101B69664` | `0x187D394` |
| 27 | `motionMemberHint_guess` (`motion`) | `0x1AB51A0` | `0x11116D4` | `0x101B69668` | `0x187D398` |
| 28 | `nodeFrameFlagsMemberHint_guess` (`flags`) | `0x1AB51A4` | `0x11116D8` | `0x101B6966C` | `0x187D39C` |
| 29 | `nodeFrameDtMemberHint_guess` (`dt`) | `0x1AB51A8` | `0x11116DC` | `0x101B69670` | `0x187D3A0` |
| 30 | `nodeFrameDocmplMemberHint_guess` (`docmpl`) | `0x1AB51AC` | `0x11116E0` | `0x101B69674` | `0x187D3A4` |
| 31 | `nodeFrameDofstMemberHint_guess` (`dofst`) | `0x1AB51B0` | `0x11116E4` | `0x101B69678` | `0x187D3A8` |
| 32 | `nodeFrameDtgtMemberHint_guess` (`dtgt`) | `0x1AB51B4` | `0x11116E8` | `0x101B6967C` | `0x187D3AC` |
| 33 | `nodeFrameTimeOffsetMemberHint_guess` (`timeOffset`) | `0x1AB51B8` | `0x11116EC` | `0x101B69680` | `0x187D3B0` |
| 34 | `nodeFrameModelMemberHint_guess` (`model`) | `0x1AB51BC` | `0x11116F0` | `0x101B69684` | `0x187D3B4` |
| 35 | `nodeFrameLoopMemberHint_guess` (`loop`) | `0x1AB51C0` | `0x11116F4` | `0x101B69688` | `0x187D3B8` |
| 36 | `nodeFramePrtMemberHint_guess` (`prt`) | `0x1AB51C4` | `0x11116F8` | `0x101B6968C` | `0x187D3BC` |
| 37 | `nodeFrameTriggerMemberHint_guess` (`trigger`) | `0x1AB51C8` | `0x11116FC` | `0x101B69690` | `0x187D3C0` |
| 38 | `nodeFrameFminMemberHint_guess` (`fmin`) | `0x1AB51CC` | `0x1111700` | `0x101B69694` | `0x187D3C4` |
| 39 | `nodeFrameFmaxMemberHint_guess` (`fmax`) | `0x1AB51D0` | `0x1111704` | `0x101B69698` | `0x187D3C8` |
| 40 | `nodeFrameVminMemberHint_guess` (`vmin`) | `0x1AB51D4` | `0x1111708` | `0x101B6969C` | `0x187D3CC` |
| 41 | `nodeFrameVmaxMemberHint_guess` (`vmax`) | `0x1AB51D8` | `0x111170C` | `0x101B696A0` | `0x187D3D0` |
| 42 | `nodeFrameAminMemberHint_guess` (`amin`) | `0x1AB51DC` | `0x1111710` | `0x101B696A4` | `0x187D3D4` |
| 43 | `nodeFrameAmaxMemberHint_guess` (`amax`) | `0x1AB51E0` | `0x1111714` | `0x101B696A8` | `0x187D3D8` |
| 44 | `nodeFrameZminMemberHint_guess` (`zmin`) | `0x1AB51E4` | `0x1111718` | `0x101B696AC` | `0x187D3DC` |
| 45 | `nodeFrameZmaxMemberHint_guess` (`zmax`) | `0x1AB51E8` | `0x111171C` | `0x101B696B0` | `0x187D3E0` |
| 46 | `nodeFrameRangeMemberHint_guess` (`range`) | `0x1AB51EC` | `0x1111720` | `0x101B696B4` | `0x187D3E4` |
| 47 | `nodeFrameCameraMemberHint_guess` (`camera`) | `0x1AB51F0` | `0x1111724` | `0x101B696B8` | `0x187D3E8` |
| 48 | `nodeFrameFovMemberHint_guess` (`fov`) | `0x1AB51F4` | `0x1111728` | `0x101B696BC` | `0x187D3EC` |
| 49 | `nodeFrameTargetMemberHint_guess` (`target`) | `0x1AB51F8` | `0x111172C` | `0x101B696C0` | `0x187D3F0` |
| 50 | `nodeFrameAnchorMemberHint_guess` (`anchor`) | `0x1AB51FC` | `0x1111730` | `0x101B696C4` | `0x187D3F4` |
| 51 | `nodeFrameFeedbackMemberHint_guess` (`feedback`) | `0x1AB5200` | `0x1111734` | `0x101B696C8` | `0x187D3F8` |
| 52 | `nodeFrameTimespanMemberHint_guess` (`timespan`) | `0x1AB5204` | `0x1111738` | `0x101B696CC` | `0x187D3FC` |

## 跨函数共享身份

对 53 个槽逐个 fresh xref 后，只有下列七项进入第二个或更多 source-level function family：

| slot | four-reference unique consumer roles |
|---|---|
| `src` | mergeContent、buildRenderCommands、renderToCanvas、accurate separate-layer render、calcViewParam、getCommandList、buildPrivateMotionGLLCommands |
| `coord` | mergeContent、calcViewParam、getCommandList |
| `color` | mergeContent、SourceCache load/constructor、Player constructor、calcViewParam、getCommandList |
| `angle` | mergeContent、updateLayers vertex computation |
| `mesh` | mergeContent、getCommandList |
| `bezierPatch` | mergeContent、getCommandList |
| `motion` | mergeContent、live Player load-motion request construction；Android 另保留一个读取未初始化 Player 尾指针的零-xref residual implementation |

强制 fresh recompile 的完整 unique consumer 地址如下。Android A64/A32 的额外 load-request
implementation 分别是 `0x6CD42C` / `0x593F60`。V257 对它们的完整函数体、零 caller 和
Player 尾布局重新闭合后，已确认它们不是 live helper 的同语义编译器 clone：它们读取一个
constructor 从不初始化的 final raw-dispatch slot，并把 callback result 与 findMotion 返回槽
分开。iOS 两端裁掉整个 residual consumer，但对象大小仍保留 pointer-width 尾成员。这里的
共同点只有 `motion` hint identity/request 形状，不能再把 raw xref 数量差解释成活跃路径的
代码折叠。完整边界见
`analysis/motionplayer_player_final_tail_dispatch_residual_four_binary_2026-08-18.md`。

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| mergeContent | `0x68FE90` | `0x56F06C` | `0x1000F1970` | `0xEDD80` |
| loadMotion | `0x6AE2F0` | `0x57F654` | `0x1001067BC` | `0x103BBC` |
| buildRenderCommands | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| renderToCanvas | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |
| accurate separate layer | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |
| calcViewParam | `0x6CE908` | `0x594958` | `0x1001201CC` | `0x11EED4` |
| getCommandList | `0x67F900` | `0x595FF0` | `0x100121EB0` | `0x120CF8` |
| private MotionGLL commands | `0x6DBB18` | `0x59CB20` | `0x10012B7D0` | `0x12A304` |
| SourceCache | `0x6A4CD4` | `0x57ACC8` | `0x1001009AC` | `0xFDB50` |
| Player constructor | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |
| vertex computation | `0x6B98D0` | `0x5866F8` | `0x10010F6AC` | `0x10CE30` |

## mergeContent 实际读取序列与复用

打开所有 mask 且启用 crossfade 时，命名读取实际序列为：

```text
src, icon(probe), icon(value), ox, oy, coord,
bm, color, opa, fx, fy, angle, zx, zy, sx, sy,
ti, ccc, occ, acc, zcc, scc, cp,
mesh, obj, cc, mcc, bp, bezierPatch, count,
motion, mask, flags, dt, docmpl, dofst, dtgt, timeOffset,
model, timeOffset, loop, dt, dtgt,
prt, mask, trigger, fmin, fmax, vmin, vmax, amin, amax, zmin, zmax, range,
camera, fov, target, anchor, target, feedback, timespan
```

这里共有 62 次 named PropGet，但不是 62 个 hint 槽：

- 两次 icon 均为 null；
- points.count 仍为 null；
- 两次 nested mask 都指向 family 之前的 `maskMemberHint_guess`；
- model 的 timeOffset/dt/dtgt 与 motion block 复用同一指针；
- anchor.target 与 camera.target 复用同一指针；
- mesh/obj、cc/mcc、bp/bezierPatch fallback 的每个字段仍各有自己的槽，不共享 fallback
  pair 的指针。

所有读取 flags 都为 0，只有 icon 的第一次存在性 probe 使用 `TJS_MEMBERMUSTEXIST`。hint
身份不会改变 V152 已闭合的 ordinary HRESULT ignore、receiver==objthis、逐字段 commit 或 owner
析构边界。

## portable 源码改动

- `MotionDispatch.h` / `RuntimeSupport.cpp` 按 native 顺序声明并定义完整 53-slot family；
- 把 `commandSrcMemberHint_guess` 与 `calcSrcMemberHint_guess` 合并为单一
  `srcMemberHint_guess`，并接到 command/query/render/SourceCache 全部已证实 callsite；
- 删除 `calcCoordMemberHint_guess`、`calcColorMemberHint_guess`，calcViewParam 改为复用
  `coordMemberHint_guess`、`colorMemberHint_guess`；
- 保留已有 `angleMemberHint_guess`、`meshMemberHint_guess`、
  `bezierPatchMemberHint_guess` 的共享身份，并把声明/定义移入准确 family 顺序；
- 新增 `motionMemberHint_guess`，mergeContent 与 `loadMotionResult_guess` 请求 Dictionary 的
  motion PropSet 共用；删除过时的 file-local `requestMotionMemberHint_guess`；
- 其余 46 项采用 `nodeFrame*MemberHint_guess` 名称，避免仅凭同名字面量与其他未证实槽
  错误合并；
- mergeContent 每个 named getter 都传入准确槽；`copyRawCurveVariant` 新增 hint 参数；
- icon 两次调用都显式传 `nullptr`，防止以后“补全”出不存在的 icon global。

## 回归探针

新增 `NodeFrameMergeHint*` probe tree，在单次 merge 中打开全部 mask：

1. 捕获完整 62 项 member、flags、hint pointer 序列；
2. 精确断言 family 中 53 个 unique pointer 的顺序；
3. 精确断言 icon 两次和 count 一次为 null；
4. 精确断言 motion/prt 的 mask 指针复用 `maskMemberHint_guess`；
5. 精确断言 motion/model 的 timeOffset/dt/dtgt 和 camera/anchor 的 target 指针相同；
6. 保持每次 receiver==objthis，并让 mesh/obj、cc/mcc、bp/bezierPatch fallback 全部发生；
7. V152 owner test 原先只记录 coord hint vector 长度，本轮收紧为准确
   `coordMemberHint_guess` 指针。

## IDB 回写

四份 recovery IDB 均完成：

- 用 53 个独立 `unsigned int` data item 替换原聚合/邻接表达，并写入与 portable 一致的
  `_guess` 名；
- 每个槽写入 index、字段和 process-wide family 注释；七个 shared slot 额外写入 consumer
  family；
- mergeContent、loadMotion 和 Android 第二个 request implementation 写入共享/复用注释；
- bookmark `V153 complete 53-slot merge member-hint family`；
- 对全部 shared-slot consumer 强制 fresh recompile：A64/A32 各 12 个，iOS 两端各 11 个；
- fresh global readback 每库恰好 53 项且全部 `size=4`；fresh loadMotion decompile 四端均读回
  `motionMemberHint_guess`；
- 部分编译器生成的 angle 地址 materialization 在 Hex-Rays 中仍显示 raw absolute MEMORY，
  但四端 global 名、精确 data xref 和两个 consumer 身份均已读回，并在对应 code heads 标注；
- 四份数据库保存。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅同一既有 warning。
- `Web Debug Build` 完整链接通过；`index.wasm` 为 85,647,102 bytes。
- `Wasmtime Headless Debug Build` 完整链接通过；`index.wasm` 为 84,994,243 bytes。
- Node `WebAssembly.Module` 解析成功：Web 539 imports / 69 exports，headless 538 imports /
  69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；因此这里只报告 probe 编译通过，
  不虚报 runtime CTest 执行。
- `git diff --check` 通过且无 trailing whitespace；工作树只有既有 LF→CRLF 提示。

## 下一纵切面

V154 应沿 `Player_loadMotion_guess` 邻近的 request/callback hint family，fresh 闭合 `chara`、
`onFindMotion`、`findMotion` 与 callback-result property 的四端地址及共享面，继续清理当前
file-local placeholder；不能因 `motion` 已闭合就推定相邻槽身份。
