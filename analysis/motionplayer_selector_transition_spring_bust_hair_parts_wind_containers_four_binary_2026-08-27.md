# motionplayer selector / transition / spring / bust / hair / parts / wind 容器总审计（四参考二进制，2026-08-27）

## 1. 范围与结论

本报告逐要求闭合 `tasks.md` 的 `MP-C08`。范围是七个相互连接的 container family：

- Engine deque #1：simple spring nodes；
- Engine deque #2/#3：hair/parts chain spring nodes；
- Engine deque #8：transition owners；
- Engine deque #9：selector owners、borrowed option vector、dormant targets vector；
- Selector 内部 12-byte command deque 和 option vector；
- Simple/Chain spring 的 fixed in-object state以及对wind pool的borrow；
- Wind emitter 的128×12-byte fixed slot pool和raw single owner。

同时审计它们的metadata builders、owner publication、progress消费顺序、selector enqueue/
step/reset、spring wrappers/solvers、wind set/step/lookup/stop、clear、ordinary destruction以及
empty/duplicate/sparse/NaN/allocation/reentrant/malformed边界。

本轮只使用四份当前参考 IDB 的原生 decompile/disasm/xref，共 fresh 审计
**103 个不同函数范围、15,488 条完整指令、307 条 xref**：

| 目标 | 不同函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | 25 | 5,026 |
| Android armv7 | 26 | 3,121 |
| iOS arm64 | 26 | 3,154 |
| iOS armv7 | 26 | 4,187 |

103/103 fresh decompile成功，103/103 disassembly `cursor.done=true`，没有截断。
Android arm64内联wind lookup、chain post-bend和三次outer-force step helper，所以它比其他
三端少一个独立函数范围；这是明确的inline disposition，不是缺失证据。

结论：本地四个目标deque、Selector两种内部container、Spring fixed records、Wind fixed
pool以及owner/borrow/clear/dtor顺序均匹配四端共同证据。真正的32/64位wind stop谓词差异
也已经用source-level条件编译保留。本轮没有 semantic C++ edit；`MP-C08` 可标为
`CLOSED_STATIC`。正式构建与运行验证仍由 `MP-V*` 独立承接。

## 2. fresh 四端核心矩阵

数字均为本轮完整反汇编指令数。

| 语义实体 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| simple spring builder | `0x6683F8` / 520 | `0x55659C` / 328 | `0x1001A7DDC` / 250 | `0x1A730C` / 423 |
| shared chain builder | `0x668DB0` / 871 | `0x556B84` / 558 | `0x1001A87C0` / 416 | `0x1A7DCC` / 664 |
| transition builder | `0x66A8A4` / 269 | `0x557B84` / 173 | `0x1001A9C9C` / 131 | `0x1A9314` / 214 |
| selector builder | `0x66ACDC` / 593 | `0x557E04` / 331 | `0x1001AA030` / 412 | `0x1A96D8` / 626 |
| Selector ctor | `0x66B778` / 58 | `0x5583B6` / 33 | `0x1001B7DFC` / 33 | `0x1B75EC` / 73 |
| Selector apply | `0x665490` / 77 | `0x5549B8` / 85 | `0x1001A5514` / 75 | `0x1A4B04` / 84 |
| Selector enqueue | `0x6655C4` / 108 | `0x554AB8` / 62 | `0x1001A5640` / 37 | `0x1A4C10` / 40 |
| Selector reset | `0x665774` / 55 | `0x554B68` / 36 | `0x1001A56D4` / 44 | `0x1A4C7E` / 43 |
| Selector step | `0x665850` / 64 | `0x554BC4` / 54 | `0x1001A5790` / 60 | `0x1A4CF6` / 70 |
| selector sync | `0x66E0FC` / 148 | `0x559A8C` / 105 | `0x1001AC8A4` / 131 | `0x1AC0D0` / 195 |
| Engine progress consumer | `0x67A3F8` / 302 | `0x55FEF0` / 95 | `0x1001B4304` / 89 | `0x1B3E10` / 104 |
| simple-spring deque wrapper | `0x678B28` / 138 | `0x55EE98` / 152 | `0x1001B29D0` / 163 | `0x1B24D8` / 195 |
| chain deque wrapper | `0x6790C8` / 235 | `0x55F2F4` / 191 | `0x1001B2F2C` / 198 | `0x1B2ABC` / 221 |
| simple spring solver | `0x65FB48` / 128 | `0x551910` / 139 | `0x1001A1A8C` / 127 | `0x1A0BE0` / 159 |
| chain spring solver | `0x665D84` / 289 | `0x555010` / 267 | `0x1001A5BDC` / 277 | `0x1A51CC` / 302 |
| chain post-bend | inline | `0x555408` / 59 | `0x1001A6030` / 53 | `0x1A5634` / 58 |
| wind force lookup | inline | `0x554FA0` / 32 | `0x1001A5B78` / 25 | `0x1A5160` / 32 |
| Engine setWind | `0x66DD8C` / 84 | `0x559900` / 114 | `0x1001AC718` / 99 | `0x1ABF24` / 120 |
| Wind ctor | `0x66DEDC` / 136 | setWind内联 | setWind内联 | setWind内联 |
| Wind step | `0x665BC8` / 85 | `0x554E4C` / 99 | `0x1001A5A24` / 85 | `0x1A4FEC` / 110 |
| primary stopWind | `0x67EE18` / 11 | `0x561D90` / 9 | `0x1001B5CD8` / 11 | `0x1B5944` / 9 |
| clear simple deque | `0x6800DC` / 112 | `0x5629A4` / 24 | `0x1001B7164` / 76 | `0x1B6D4E` / 81 |
| clear chain deque | `0x68029C` / 45 | `0x562B80` / 24 | `0x1001B7298` / 78 | `0x1B6E2A` / 73 |
| clear transition deque | `0x680D1C` / 91 | `0x5633C0` / 24 | `0x1001B78A0` / 73 | `0x1B723C` / 80 |
| clear selector deque | `0x680F14` / 53 | `0x563560` / 24 | `0x1001B79C8` / 80 | `0x1B7318` / 80 |
| metadata reset | `0x666D08` / 250 | `0x555AD8` / 32 | `0x1001A67BC` / 34 | `0x1A5F4C` / 32 |
| Engine ordinary dtor | `0x67C898` / 304 | `0x5610E8` / 71 | `0x1001B8B4C` / 97 | `0x1B814E` / 99 |

## 3. 容器与所有权拓扑

```text
EmoteEngine
  deque #1<SimpleNode>
    node unique-owns EmoteSpringState (fixed 72B; no nested heap)
  deque #2<ChainNode>
    node unique-owns EmoteBustChainSpring (fixed 0xB0/0xA8)
  deque #3<ChainNode>
    same source element type and ownership as #2

  deque #8<TransitionEntry>
    entry unique-owns EmoteVarController
      controller owns 20B keyframe deque + three raw float arrays

  deque #9<SelectorEntry>
    entry unique-owns EmoteSelectorController
      owns 12B command deque
      owns vector<SelectorOption>
        option.refCtl borrows deque #8 controller pointee
    entry owns ttstr label
    entry owns vector<TransitionEntry*> targets
      native builder leaves it empty; no writer in root-reachable xref closure

  raw single-owner EmoteWindEmitter
    owns no heap subobject
    embeds fixed EmoteWindParticle slots[128]
    chain spring.collisionCurve borrows this pointer
```

Engine四个deque的source element stride：

| element | 64-bit | 32-bit | 逆序非平凡析构 |
|---|---:|---:|---|
| simple node | 48 | 28 | keyY → keyX → shape → spring |
| chain node | 56 | 32 | keyC → keyB → keyA → shape → spring |
| transition entry | 24 | 12 | label → Var controller |
| selector entry | 48 | 24 | targets vector → label → Selector controller |

Selector option是64-bit 16B、32-bit 12B；command element四端均12B。Wind slot四端均12B，
整个 emitter四端均 `0x61C`（1564B）。这些fixed contracts可以写进portable source；deque/
vector header与block布局则只能作为ABI证据。

## 4. metadata producer与publication frontier

### 4.1 simple spring deque

共同顺序：

```text
raw = new EmoteSpringState(metadata)
read param.op / p / pv / ofs into raw
deque#1.emplace_back(raw)              # owner publication
entry.shape, keyX, keyY sequentially assign
resolver[keyX] = {type=0, metadataIndex}
resolver[keyY] = {type=0, metadataIndex}
```

constructor成功到emplace成功之间，raw spring没有临时owner；property/conversion/deque growth
抛出会泄漏。emplace以后任一string/map异常都保留live owner与此前字段/locator前缀。

### 4.2 chain deques

#2/#3共享一个source builder，只替换目标deque与resolver type tag：

```text
raw = new EmoteBustChainSpring(metadata)
read fixed params
decode bp -> p -> pv nested arrays into fixed spring state
targetDeque.emplace_back(raw)          # owner publication
assign shape/keyA/keyB/keyC
publish three resolver entries
```

raw leak窗口覆盖所有nested arrays。entry constructor把四个ttstr置empty、anchors置0，但故意
不写 `initFlag`。equal/empty/duplicate resolver key允许；后写locator覆盖旧值，不删除旧
deque owner。disabled metadata不append placeholder，却把resolver index保持为原metadata
index，形成unchecked sparse-hole边界。

### 4.3 transition deque

每个enabled element先构造count=1 `EmoteVarController`，然后emplace到#8，entry flag置1，
最后assign label并publish `{type=7,index}`。Var constructor的raw-array前缀失败边界延续
`MP-L07`；constructor完成后到emplace之间仍可能泄漏整个controller。

### 4.4 selector deque与borrow publication

builder顺序是：

1. 读取selector label；disabled item先remove variable label再跳过；
2. 为每个option重新从#8开头做first-equal scan；
3. 命中时保存 `transition.ctl.get()` borrow、立即把transition flag清0、remove option label；
4. 之后才读取off/on值并向local option vector push；
5. `new SelectorController(move(options))`；constructor立即apply index0；
6. selector deque emplace发布owner；
7. assign entry label，entry direct flag保持constructor-uninitialized；
8. targets vector default-empty；publish `{type=8,index}` resolver。

所以transition flag/variable-label side effect早于selector allocation和owner publication；
任何后续失败都不回滚。Selector constructor内部apply还可改变更早borrowed transition queue。

`targets` 与option vector是两条不同的borrow边。四端builder、sync、is/activate/deactivate
target的完整xref closure没有targets writer；正常native lifetime保持empty。实现不得从option
label合成targets或删除对应transition entry。

## 5. Selector内部container状态机

### 5.1 constructor与apply

constructor拥有移动进来的option vector，默认构造command deque，清
`selState/selectedIndex/invDuration/accum`，然后apply index0。apply先提交selectedIndex，再按
option vector顺序：

- null borrowed controller跳过；
- selected option取onValue，其余取offValue；
- 对borrowed controller做dt=0 step读取current；
- controller busy或 `abs(current-target)>=1e-7` 时replace target；
- duration缩放为 `abs(delta/(on-off))*duration`，没有zero-span/NaN/Inf guard；
- 早先option副作用在后续queue allocation failure时保留。

option pointer永不AddRef/delete；owner始终是Engine transition deque。

### 5.2 command enqueue

四端的FP门是 `FCMP/VCMPE duration,0` 后 `B.LE`。unordered也满足该branch，因此共同源码
判据是 `!(duration > 0)`：

- zero、negative、NaN：clear command deque，selState=0，selection按signed-int32 toward-zero
  saturated conversion后立即apply；
- positive且append=false：先clear/state=0，再push `{selection,duration,fade}`；
- positive且append=true：保留旧queue并push；
- +Inf属于positive，进入queue；
- push allocation failure不会恢复刚刚完成的clear。

selector index conversion对NaN给0，越过int32上/下界饱和，其余toward-zero。这个显式
conversion是Wasm可移植边界，不应直接依赖C++ float-to-int UB。

### 5.3 step与reset

step只有在selState=0且queue非空时取front：先copy并pop，再apply；apply抛出时该command
已经丢失。成功后写 `invDuration=1/duration`、state=1、accum=0。active state1每次累加
`invDuration*dt`，ordered `>=1`才clamp到1并idle；NaN accum保持active。输出始终是
selectedIndex转float。

reset有queue时先state=0，使用back selection做zero-duration apply，**apply成功后**才clear
queue；apply失败时queue仍在。没有queue但active时先state=0再reapply current index。

## 6. Engine消费顺序与reentry边界

每个controller slice的顺序是：eye → eyebrow → mouth → **selector → transition** → loop。
因此selector在本slice向borrowed transition写target后，transition会在同一个slice立即step。
之后root controllers和wind fixed pool step。Player progress完成后，只有原始dt非0且非
direct-edit才进入spring tail：三outer-force controller → simple deque → chain #2 → chain #3。

这些deque都按live begin/end迭代，没有snapshot。shape resolver和map/dispatch路径可能抛出；
已经step/published的前缀不回滚。若脚本可重入地reset同一Engine metadata，当前deque
iterator/node borrow会失效；参考实现没有reentry guard，不能用snapshot“修复”。

## 7. simple与chain spring container消费

simple wrapper从bust outer-force controller按其unchecked count memcpy到2-float stack buffer。
正常count为2；count<1保留uninitialized force，count>2可覆盖stack。每个node：

- 从persistent anchors开始，调用shape resolver；
- initFlag置位时清flag并整段step一次；
- 否则仅在 `dt-0.0001>0` 时用不大于1.1的substeps插值anchor；
- skipped-step路径的outX/outY故意uninitialized，仍写variable map；
- anchors先提交，再按keyX→keyY顺序upsert outputs。

chain wrapper同样unchecked copy和uninitialized output，但每个node在任何step gate之前都写：

```text
node.spring.collisionCurve = engine.windEmitter
```

随后调用fixed two-segment chain solver和post-bend，最后按keyA(segment1 X) → keyB(segment0 X)
→ keyC(selected Y)发布。map allocation failure保留earlier key和spring state前缀。

Simple spring 72B与Chain 0xB0/0xA8都不拥有dynamic container；内部全部是fixed scalar/
small arrays。Chain的wind pointer只是borrow，每次wrapper刷新，spring destructor不读取。

## 8. Wind fixed pool、owner替换与平台差异

Wind不是vector/deque，而是一个固定layout：

```text
slots[128] × 12B
  byte active
  3 bytes padding
  float lifePos
  float yPos
tail
  float startPos, endPos
  byte gate + padding
  float yHi, yLo, velocity, emitAccumulator
```

constructor只清128个active byte；inactive slot的padding/lifePos/yPos和gate padding保持
allocation内容。tail初始化为endpoints、gate0、`{yHi=1,yLo=0,velocity=0,acc=0}`。

### 8.1 setWind raw owner

先把negative amplitude转absolute并交换endpoints。stop predicate是真实平台差异：

| 平台宽度 | stop条件 |
|---|---|
| 两个64-bit | amp==0，或min==max，或freqX和freqY同时为0 |
| 两个32-bit | amp==0，或freqX==0；不读interval/freqY作为stop gate |

stop delete/null emitter，但保留五个cache。非stop时，endpoint cache不变则复用旧pool，保留
所有active particles；endpoint变化先delete旧owner，再operator new。member直到fresh object
完全初始化后才覆盖，因此allocation failure留下dangling member和旧cache。这是四端共同
raw-owner历史边界。

成功后更新五个cache，写spawn range/gate/signed velocity并把accumulator清0。metadata scale
为0/NaN时division按原FP传播；endpoint/cache NaN比较会触发replacement。

primary `Motion.EmotePlayer.stopWind`是独立delete/null，不改cache；D3D stop包装则以五个零
调用setWind，最终同样释放。

### 8.2 fixed-pool step

每个controller slice在gate置位时step：

1. `acc += abs(velocity)*dt`；
2. acc ordered `>=0` 时至少做一次chance RNG roll；每轮结束acc减1；
3. chance `<1/16` 时线性查第一个inactive slot；pool满仍消耗chance roll但不消耗y roll；
4. 有free slot才set active、lifePos=start，并用第二个RNG产生 `[yLo,yHi]` yPos；
5. 随后遍历全部128 slots，因此刚spawn的slot也在同一step移动；
6. positive velocity只在 `lifePos>end` 时kill，negative只在 `<end` 时kill；zero/NaN不kill。

inactive slot payload不清。acc或dt为NaN时ordered gate不进入emission loop，但active slot的
position继续按NaN传播。

### 8.3 chain wind lookup

lookup按index 0→127扫描active slot，使用strict interval：

```text
lifePos - (yPos/2 + 4) < segmentX < lifePos + (yPos/2 + 4)
```

返回第一个hit的 `yPos*velocity`；不累加多个slot。Android arm64内联，其他三端保留独立
helper。borrowed emitter已由chain wrapper刷新；如果Engine raw owner因setWind allocation
failure成为dangling，后续lookup会按原版发生UAF，没有防御性nulling。

## 9. clear、普通析构与borrow lifetime

metadata reset先清resolver map，再按#1→#10 declaration order clear：

- #8 transition先死，#9 selector后死；option pointers短暂dangling，但selector destructor只
  释放自己的option/command/targets storage，不解引用borrow；
- clear释放logical elements但按平台deque实现保留可复用header/map/block状态；
- duplicate resolver覆盖不影响被覆盖的旧owner，它直到clear/dtor才释放。

普通Engine destructor先delete wind raw owner，再销毁Player/controllers和后部containers，
最后按#10→#1逆序销毁metadata deques：

- #9 selector先死，#8 transition后死，正常borrow lifetime完整覆盖；
- wind先于#3/#2 chain spring死亡，collisionCurve短暂dangling，但spring/node destructor不读；
- simple/chain entry按字符串逆序后delete fixed spring；
- transition entry先label后Var controller；
- selector entry先targets vector、label，再Selector option vector/command deque/controller。

这些destructor不调用脚本，不发生reentrant callback。

## 10. 主要边界矩阵

| 边界 | 四端共同结果 |
|---|---|
| duplicate/empty variable key | resolver后写覆盖；所有旧deque owners继续存活 |
| disabled metadata hole | resolver保存raw metadata index；compacted deque无placeholder，consumer unchecked |
| builder raw pointee + property/growth throw | constructor完成后的raw pointee泄漏 |
| owner publication后label/map throw | live partial entry保留，不回滚 |
| selector first transition match | 从#8头部重新扫描；只borrow第一个equal |
| selector option side effect | transition flag/variable-label removal早于selector owner publication |
| selector targets | vector真实存在，但normal builder/xref closure没有writer，保持empty |
| selector duration NaN | unordered `B.LE`，clear并立即apply，不入queue |
| selector zero option span | unguardeddelta/span，NaN/Inf传播进scaled duration |
| selector step apply throw | front command已pop；later queue保留 |
| selector reset apply throw | state已清0；queue尚未clear |
| spring non-first且small/NaN dt | outputs未初始化仍写variable map |
| outer-force count异常 | unchecked memcpy到2-float stack buffer |
| chain wind pointer | 每node刷新borrow；setWind dangling-owner失败可产生UAF |
| wind pool full | chance RNG仍消耗，y RNG不消耗 |
| wind stopped | emitter释放，cache保留 |
| wind endpoint change allocation throw | member保留已释放旧地址 |
| 64/32 stop predicate | genuine platform behavior，不能合并成单一runtime predicate |
| reset #8 before #9 | temporary dangling option borrow，不解引用 |
| dtor wind before chain | temporary dangling collisionCurve，不解引用 |
| live-deque reentry | 无snapshot/guard；同容器mutation会使iterator/borrow失效 |

## 11. 本地逐行映射

| 语义 | 本地位置 | 结论 |
|---|---|---|
| four deque element types | `cpp/plugins/motionplayer/EmoteEngine.h:226`; `:349`; `:368` | 匹配 |
| deque declaration/owner order | `cpp/plugins/motionplayer/EmoteEngine.h:728` | 匹配 |
| wind raw owner/cache fields | `cpp/plugins/motionplayer/EmoteEngine.h:807` | 匹配 |
| selector option/command containers | `cpp/plugins/motionplayer/EmoteSelectorController.h:16` | 匹配 |
| selector ctor/enqueue/step/reset/apply | `cpp/plugins/motionplayer/EmoteSelectorController.cpp:32` | 匹配 |
| selector/transition builders | `cpp/plugins/motionplayer/EmoteEngine.cpp:1896` | 匹配 |
| simple/chain builders | `cpp/plugins/motionplayer/EmoteEngine.cpp:2328` | 匹配 |
| simple deque wrapper | `cpp/plugins/motionplayer/EmoteEngine.cpp:1626` | 匹配 |
| chain deque wrapper | `cpp/plugins/motionplayer/EmoteEngine.cpp:1695` | 匹配 |
| simple/chain fixed solvers | `cpp/plugins/motionplayer/EmoteSpring.cpp:22`; `:120` | 匹配 |
| wind first-hit lookup | `cpp/plugins/motionplayer/EmoteSpring.cpp:103` | 匹配 |
| wind fixed layout/ctor/step | `cpp/plugins/motionplayer/EmoteWindEmitter.h:35`; `cpp/plugins/motionplayer/EmoteWindEmitter.cpp:12` | 匹配 |
| pointer-width setWind | `cpp/plugins/motionplayer/PlayerCore.cpp:858` | 匹配 |
| primary dedicated stop | `cpp/plugins/motionplayer/EmotePlayer.cpp:532` | 匹配 |
| progress order | `cpp/plugins/motionplayer/EmoteEngine.cpp:3830` | 匹配 |
| reset/dtor order | `cpp/plugins/motionplayer/EmoteEngine.cpp:884`; `:946` | 匹配 |

本轮重新确认的两个易误判点无需修改：selector duration的unordered `B.LE`对应
`!(duration>0)`；wind stop predicate确实按pointer width两两分裂，本地 `INTPTR_MAX`
分支正确。

## 12. 测试映射与验证边界

已有unit源直接覆盖本任务oracle：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:11427`：spring/chain/selector element owner move；
- `:11872`、`:12162`、`:12329`、`:12355`：Selector option/queue、transition borrow与fixed states；
- `:20586`：builder publication与sparse/duplicate resolver；
- `:28324`：wind fixed pool和set/stop边界；
- `:30381`：reset/dtor与selector/transition运行顺序。

本轮只检查测试源与实现映射，没有把它宣称为已运行；正式unit/Web build仍属于
`MP-V06..V08`。

## 13. IDB改进与disposition

四份IDB已经：

- 新增38个确定性runtime helper名称；
- 每份追加26条 `MP-C08` task comment，共104条；
- 每份在selector builder root新增1个bookmark；
- 所有改动保存到四份 `.i64`。

最终 disposition：

- 原始任务：`MP-C08`；
- coverage slice：`MP-C08-SELECTOR-TRANSITION-SPRING-BUST-HAIR-PARTS-WIND-CONTAINERS`；
- evidence status：`IMPLEMENTED`；
- task status：`CLOSED_STATIC`；
- semantic C++ edit：无；
- 剩余：正式构建/运行验证，以及更广的跨容器统一边界与STL差异任务 `MP-C15/C16`。
