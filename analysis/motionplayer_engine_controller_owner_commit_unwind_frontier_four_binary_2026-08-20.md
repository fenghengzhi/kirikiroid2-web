# EmoteEngine 七个 direct-controller owner commit 与逐级异常 frontier 四参考闭环（V262）

日期：2026-08-20

## 1. 范围与新增结论

V261已经闭合 `Player` pending raw allocation → completed owner publication的外层边界；早期
`motionplayer_engine_direct_owner_unwind_four_binary_2026-08-13.md` 则证明 Player与七个 direct
controller字段都是 pointer-sized `std::unique_ptr`。本轮不重复这两个结论，而是补此前缺少的
逐 controller机器级 ledger：

- 每项精确的 allocation size、constructor参数、owner slot与 publication指令；
- allocation failure、pointee-constructor failure和publication后 failure各自从哪里进入
  Engine prefix unwind；
- iOS armv7完整 1-based SJLJ `call_site` → 0-based switch case表；
- `EmoteAngleController` 在 Android eager libstdc++ deque与 iOS lazy libc++ deque下不同的
  可抛 frontier；
- constructor body四个 seed setter失败时，为什么必须先清 trailing HM/Variant prefix，
  再销毁全部八个 direct owners。

四端共同源码仍是普通 declaration-order member initializers；本轮没有发现 portable运行时代码
偏差，不添加手写 EH或平台分支。

## 2. 四端共同 owner 顺序、对象尺寸与 slot

| owner | constructor输入 | Android A64 size / slot | Android A32 size / slot | iOS A64 size / slot | iOS A32 size / slot |
|---|---|---:|---:|---:|---:|
| position | `EmoteVarController(2)` | `0x80` / `+0x430` | `0x48` / `+0x218` | `0x60` / `+0x2C0` | `0x38` / `+0x160` |
| scale | `EmoteVarController(1)` | `0x80` / `+0x438` | `0x48` / `+0x21C` | `0x60` / `+0x2C8` | `0x38` / `+0x164` |
| color | `EmoteVarController(4)` | `0x80` / `+0x440` | `0x48` / `+0x220` | `0x60` / `+0x2D0` | `0x38` / `+0x168` |
| angle | `EmoteAngleController()` | `0x70` / `+0x448` | `0x44` / `+0x224` | `0x50` / `+0x2D8` | `0x34` / `+0x16C` |
| bust outer force | `EmoteVarController(2)` | `0x80` / `+0x450` | `0x48` / `+0x228` | `0x60` / `+0x2E0` | `0x38` / `+0x170` |
| hair outer force | `EmoteVarController(2)` | `0x80` / `+0x458` | `0x48` / `+0x22C` | `0x60` / `+0x2E8` | `0x38` / `+0x174` |
| parts outer force | `EmoteVarController(2)` | `0x80` / `+0x460` | `0x48` / `+0x230` | `0x60` / `+0x2F0` | `0x38` / `+0x178` |

这些 slot紧跟 V261的 Player owner，按 pointer width无洞连续；parts publication之后紧接 raw
wind/cache/state区域。count序列严格为 `2,1,4,<angle>,2,2,2`，三个 outer-force controller
没有 constructor-body seed setter。

## 3. 七组 allocation → constructor → publication 地址总账

地址格式为 `operator new call / constructor call-or-inline-range / owner store`。只在所属二进制
内有效。

| owner | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| position | `0x67BA34 / 0x67BA44 / 0x67BA48` | `0x560AD4 / 0x560ADE / 0x560AE2` | `0x1001B8054 / 0x1001B8064 / 0x1001B8068` | `0x1B786C / 0x1B787A / 0x1B7882` |
| scale | `0x67BA50 / 0x67BA60 / 0x67BA64` | `0x560AE8 / 0x560AF2 / 0x560AF6` | `0x1001B8070 / 0x1001B8080 / 0x1001B8084` | `0x1B788C / 0x1B789A / 0x1B78A2` |
| color | `0x67BA6C / 0x67BA7C / 0x67BA80` | `0x560AFC / 0x560B06 / 0x560B0A` | `0x1001B808C / 0x1001B809C / 0x1001B80A0` | `0x1B78AC / 0x1B78BA / 0x1B78C2` |
| angle | `0x67BA88 / 0x67BA8C..0x67BAB0 / 0x67BAB4` | `0x560B10 / 0x560B14..0x560B2C / 0x560B2E` | `0x1001B80A8 / 0x1001B80AC..0x1001B80BC / 0x1001B80C0` | `0x1B78CC / 0x1B78D0..0x1B78E6 / 0x1B78EA` |
| bust | `0x67BABC / 0x67BACC / 0x67BAD0` | `0x560B34 / 0x560B3E / 0x560B42` | `0x1001B80C8 / 0x1001B80D8 / 0x1001B80DC` | `0x1B78F4 / 0x1B7902 / 0x1B790A` |
| hair | `0x67BAD8 / 0x67BAE8 / 0x67BAEC` | `0x560B48 / 0x560B52 / 0x560B56` | `0x1001B80E4 / 0x1001B80F4 / 0x1001B80F8` | `0x1B7914 / 0x1B7922 / 0x1B792A` |
| parts | `0x67BAF4 / 0x67BB04 / 0x67BB14` | `0x560B5C / 0x560B66 / 0x560B70` | `0x1001B8100 / 0x1001B8110 / 0x1001B8114` | `0x1B7934 / 0x1B7942 / 0x1B7956` |

每一行的 store都严格晚于 constructor正常返回或 inline constructor最后一个可能失败操作；没有
任何端先把 raw pointer塞进 owner slot再尝试构造 pointee。

## 4. 共同三类 failure frontier

对任一 `EmoteVarController` owner，四端共同源码语义为：

```cpp
// member initializer generated semantics
void *pending = operator new(sizeof(EmoteVarController));
try {
    EmoteVarController *complete =
        new (pending) EmoteVarController(count);
    owner = std::unique_ptr<EmoteVarController>(complete);
} catch (...) {
    operator delete(pending);
    destroy_completed_earlier_members_in_reverse_order();
    throw;
}
```

机器级边界必须区分：

1. `operator new` 自身抛出：没有 raw allocation可删，直接从前一个 completed owner开始；
2. pointee constructor抛出：先只 `operator delete(pending)`，不能调用完整 pointee destructor，
   再从前一个 completed owner开始；
3. publication后的 later failure：当前 owner已完成，逆序链会调用完整 controller destructor和
   `operator delete`。

portable代码应只写正常 C++ member initializer。显式给所有 slot预清零、统一从 parts开始扫
null slot、或给每个 initializer手写 catch都会改变 partial-object lifecycle和生成结构。

## 5. Angle：共同源码、两种标准库 frontier

`EmoteAngleController` 源码包含一个空 `std::deque<12-byte keyframe>` 和 scalar tail；四端源码
结构相同，生成的可抛边界却不同。

### 5.1 Android old-libstdc++

Android空 deque采用 eager allocation：

- A64在 `0x67BAA8` 调用 `EmoteAngleKeyframeDeque_init_guess`；
- A32在 `0x560B24` 调用同义 initializer；
- initializer会分配 deque map与首 block，因而可以在 Angle allocation已经存在、owner尚未
  publication时抛出；
- A64明确保留 `0x67BF30` pending-Angle raw-delete landing，随后从 completed color开始回滚。

### 5.2 iOS libc++

iOS空 deque是 lazy zero header：

- A64 `0x1001B80AC..0x1001B80BC` 只写零；
- A32 `0x1B78D0..0x1B78E6` 只写零；
- allocation后没有 constructor call或其它可抛操作，随即 publication；
- 所以 SJLJ只有 Angle allocation `call_site 9`，没有“Angle constructor pending pointer”状态；
- iOS A64 unwind helper同样只有 `0x1001B82E4` allocation-failure entry，直接从 completed
  color开始。

这不是平台源代码 `#ifdef`，也不应在 portable类中手工模拟：同一个 default-constructed
`std::deque` 由各平台 STL自然生成两种边界。

## 6. Android arm64 pending-constructor ladder

Android A64每个可抛 controller constructor都保留独立 landing。表中的“下一 completed owner”
是 pending raw delete后开始完整析构的第一个 owner。

| failing constructor | pending-delete entry | 下一 completed owner / slot |
|---|---:|---|
| parts | `0x67BEB8` | hair `+0x458` |
| hair | `0x67BEE0` | bust `+0x450` |
| bust | `0x67BF08` | angle `+0x448` |
| angle eager deque | `0x67BF30` | color `+0x440` |
| color | `0x67BF58` | scale `+0x438` |
| scale | `0x67BF80` | position `+0x430` |
| position | `0x67BFA8` | Player `+0x428` |

completed-owner destructor blocks从不同 entry汇入同一逆序链：

```text
parts +0x460 @ 0x67C174
hair  +0x458 @ 0x67C190
bust  +0x450 @ 0x67C1AC
angle +0x448 @ 0x67C1C8
color +0x440 @ 0x67C214
scale +0x438 @ 0x67C230
pos   +0x430 @ 0x67C24C
Player+0x428 @ 0x67C268
```

每个 block都执行完整 pointee destruction + delete，再清 owner slot；landing直接跳入第一个
实际完成的 block，未构造 member不会被读取。

## 7. iOS arm64多入口 helper

`EmoteEngine_ctor_unwind_cleanup_guess`（`0x1001B829C`）开头是一组 compiler-generated entry
stubs，精确编码 allocation/constructor二分：

| owner | allocation-failure entry → earlier owner | constructor-failure entry → pending delete → earlier owner |
|---|---|---|
| parts | `0x1001B82AC` → hair | `0x1001B829C` → `0x1001B8398` → hair |
| hair | `0x1001B82C4` → bust | `0x1001B82B4` → `0x1001B83AC` → bust |
| bust | `0x1001B82DC` → angle | `0x1001B82CC` → `0x1001B83C0` → angle |
| angle | `0x1001B82E4` → color | 无 constructor-failure entry |
| color | `0x1001B82FC` → scale | `0x1001B82EC` → `0x1001B83E8` → scale |
| scale | `0x1001B8314` → position | `0x1001B8304` → `0x1001B83FC` → position |
| position | `0x1001B832C` → Player | `0x1001B831C` → `0x1001B8410` → Player |

later-member或 constructor-body failure从 `0x1001B8348` 进入：先清 HM7/HM6/HM5/HM4与三个
Variant，再从 parts slot `+0x2F0` 开始完整八-owner逆序链。iOS libc++ owner block都先把
slot清零，再 destroy/delete pointee；这是标准库 lowering差异，不是源码显式 exchange。

## 8. iOS armv7 SJLJ完整 state表

constructor把 1-based `call_site`写入 SJLJ frame；dispatcher
`EmoteEngine_ctor_sjlj_unwind_dispatch_guess` 使用 `call_site - 1` 作为 0-based case。

| stored call_site | switch case | protected operation | 失败动作 |
|---:|---:|---|---|
| 1 | 0 | Player allocation | 无 raw，直接早期 Engine prefix |
| 2 | 1 | Player constructor | delete pending Player raw，直接早期 prefix |
| 3 | 2 | position allocation | 从 completed Player开始 |
| 4 | 3 | position constructor | delete pending position raw，再从 Player开始 |
| 5 | 4 | scale allocation | 从 completed position开始 |
| 6 | 5 | scale constructor | delete pending scale raw，再从 position开始 |
| 7 | 6 | color allocation | 从 completed scale开始 |
| 8 | 7 | color constructor | delete pending color raw，再从 scale开始 |
| 9 | 8 | angle allocation | 无 raw，直接从 completed color开始；无 Angle ctor state |
| 10 | 9 | bust allocation | 从 completed angle开始 |
| 11 | 10 | bust constructor | delete pending bust raw，再从 angle开始 |
| 12 | 11 | hair allocation | 从 completed bust开始 |
| 13 | 12 | hair constructor | delete pending hair raw，再从 bust开始 |
| 14 | 13 | parts allocation | 从 completed hair开始 |
| 15 | 14 | parts constructor | `LABEL_14` delete pending parts raw，再从 hair开始 |
| 16 | 15 | position seed setter | 清 trailing prefix，再从 completed parts开始 |
| 17 | 16 | scale seed setter | 同上 |
| 18 | 17 | angle seed setter | 同上 |
| 19 | 18 | color seed setter | 同上 |
| 20 | 19 | cleanup再抛/terminate state | `abort()` |

completed owner labels为：parts → `LABEL_23` hair → `LABEL_25` bust → `LABEL_27` angle →
`LABEL_29` color → `LABEL_31` scale → `LABEL_33` position → `LABEL_35` Player →
`LABEL_37` earlier Engine prefix。每个 label先清 slot再 destroy/delete。

四个 seed state说明 constructor body不是“对象已经完全构造，抛出就调用完整 Engine dtor”；
仍然是 constructor failure，按已完成 member逆序回滚。由于 HM4–HM7和三个 Variant在 owner之后
声明，它们先于 parts owner被销毁。

## 9. Android armv7边界

Android armv7正常 constructor清晰保留七组 `new/ctor/store`与 Android eager Angle deque call，
但本 IDB的 `.ARM.extab`/shared landing fragments没有被 Hex-Rays恢复成一个可独立反编译的
Engine ctor-unwind function；owner destructor helper xrefs也只显式归入 normal Engine dtor和其它
使用点。故本轮不伪造一个 A32 EH函数地址。

可以直接从本端证明的部分是：

- 每个 pointer只在其 constructor/inline initializer正常完成后才 store；
- Angle publication前有 eager deque allocation call；
- normal dtor的 `EmoteVarController`/Angle/Player owner-slot specializations与字段逆序精确匹配；
- 与 Android A64同编译器/stdlib家族、与两个 iOS端同一源码顺序联合后，direct member
  new-expression是唯一同时解释四端的共享源码结构。

报告明确保留这项 IDA恢复质量差异，不把一次 negative function search当作“Android A32没有
异常回滚”。

## 10. 本地源码逐项对照与过时记录纠正

当前 `EmoteEngine.h` 连续声明：

```cpp
std::unique_ptr<Player> _player;
std::unique_ptr<EmoteVarController> _ctlPosition;
std::unique_ptr<EmoteVarController> _ctlScale;
std::unique_ptr<EmoteVarController> _ctlColor;
std::unique_ptr<EmoteAngleController> _ctlAngle;
std::unique_ptr<EmoteVarController> _ctlBustOuterForce;
std::unique_ptr<EmoteVarController> _ctlHairOuterForce;
std::unique_ptr<EmoteVarController> _ctlPartsOuterForce;
```

`EmoteEngine.cpp` initializer严格为：

```cpp
: _player(new Player(rmDispatch)),
  _ctlPosition(new EmoteVarController(2)),
  _ctlScale(new EmoteVarController(1)),
  _ctlColor(new EmoteVarController(4)),
  _ctlAngle(new EmoteAngleController()),
  _ctlBustOuterForce(new EmoteVarController(2)),
  _ctlHairOuterForce(new EmoteVarController(2)),
  _ctlPartsOuterForce(new EmoteVarController(2))
```

它已经逐项生成第 2–8节的 commit/frontier；无需运行时代码修改。V262只就地纠正
`motionplayer_engine_direct_owner_unwind_four_binary_2026-08-13.md` 两条过时说明：

- 把旧“constructor body `owner.reset(new T(...))`”更新为真实 member initializer；
- 把旧“pending delete后总是 parts→...→Player”更新为每个失败点从前一个 completed owner
  进入的多入口阶梯，并补 Angle STL差异。

compiled source没有加入本端地址、硬编码 offset、手写 try/catch或平台特判。

## 11. recovery IDB 写回

本轮共写回 **53 comments / 53 bookmarks / 5 semantic renames**：

- Android arm64：14 comments/bookmarks（7 owner commits + 7 pending-constructor landings）；
- Android armv7：8 comments/bookmarks（7 owner commits + eager Angle allocation frontier）；
- iOS arm64：15 comments/bookmarks（7 owner commits + 7 representative entry stubs +
  post-owner failure entry）；
- iOS armv7：16 comments/bookmarks（7 owner commits + SJLJ switch/frontiers + seed cleanup）。

iOS armv7 semantic renames（无二进制名字字面，故全部保留 `_guess`）：

- `0x1B7788` → `EmoteEngine_ctor_guess`；
- `0x1B7B02` → `EmoteEngine_ctor_sjlj_unwind_dispatch_guess`；
- `0x1A3FEC` → `EmoteVarController_ctor_guess`；
- `0x1C1D62` → `EmoteVarController_dtor_guess`；
- `0x1B6A38` → `EmoteAngleKeyframeDeque_dtor_guess`。

iOS armv7 different-path安全保存：

- pre-V262 backup：
  `out/idb-recovery/v262-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v262.i64`，
  377,673,936 bytes，SHA-256
  `103AB7A20EB11566E0B5364B08C458224617BBC26567F94CB9442B65A170A5D6`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v262.i64`；
- `C:\IDA\idat.exe -A` candidate probe退出 0；
- candidate/canonical resolved path覆盖前均确认位于 workspace；
- candidate与最终 canonical均为 377,731,280 bytes，SHA-256
  `53098CD827856C0483374F5692A7569D2B5439B01877B59D0CB99D703465AA81`；
- canonical重新打开后回读 16条 V262 comments与5个 semantic names，再关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,736,948 | `A1422B7F18E0BD1B38185CB9D8304840C477EF123941FD781F7CD0C5B76D5441` |
| Android armv7 | 345,878,941 | `C2F8A62D08F5FE8CC0E02047062F646216751B6EAE8F672CDB7857F69A6F5383` |
| iOS arm64 | 334,933,959 | `AC0687E841712C27235C8FC7261358BF96DAFB2D2D2CBFC3E2F3EF12C6E98AE0` |
| iOS armv7 | 377,731,280 | `53098CD827856C0483374F5692A7569D2B5439B01877B59D0CB99D703465AA81` |

最终 IDA MCP session与本机 IDA process均为 0。

## 12. 验证与产物基线

V262没有修改 compiled C/C++，只更新 IDB和分析文档。仍 fresh执行：

- ordinary motionplayer test TU syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- 两次 syntax-only都只出现仓库既有 `_tss` deprecated warning；
- Web Debug：`ninja: no work to do`；
- Wasmtime Headless Debug：`ninja: no work to do`；
- guest：`ninja: no work to do`；
- `git diff --check`退出 0，仅既有 LF→CRLF warning；
- IDA process/session均为 0。

Wasm保持 V261最终基线：

| wasm | size | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85,655,322 | `E58F27436614228AD4FC9D13A7F79A13269330FF4FD22209466C214BFC68B260` |
| Wasmtime `index.wasm` | 85,002,463 | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` |
| guest | 151,479,107 | `FC8AC9D73004DE8234F47D193385283D232E43E2F39CA9B20149639C23473993` |

## 13. 本轮闭合与后续方向

V262把 Engine的八个 direct owners从“类型与高层顺序正确”推进到逐项 commit、逐项 pending raw
和逐项 EH frontier均可追溯。下一高价值纵切面不应再次审计同一 owner链，而应从 parts
publication之后继续：精确闭合 wind/cache/trigger/double/Variant/HM4–HM7这些 trailing members
在 constructor中各自的可抛/不可抛 frontier，以及四个 seed setter抛出时 trailing prefix的完整
局部析构顺序。这样可把 V262的 cases 15..18前半段展开成字段级账本，并检查现有 Engine
constructor总览是否仍含“所有 trailing fields先完成、再 seed”的过时简写。
