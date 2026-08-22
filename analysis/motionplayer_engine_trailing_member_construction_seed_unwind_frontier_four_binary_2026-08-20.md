# MotionPlayer `EmoteEngine` trailing member 构造、seed failure 与回滚 frontier 四参考复原

日期：2026-08-20
纵切面：V263

## 1. 结论

本轮重新顺序打开并核对 `reference/binaries/` 中四份当前 1.3.9 参考，闭合
`EmoteEngine` 在 parts controller owner 发布之后的完整尾部：

```text
raw wind owner
five float wind/cache values
eight trigger/state bytes
five doubles
three tTJSVariant members
HM4 instant-variable set
HM5 variable-range map
HM6 variable-controller-ref map
HM7 label-value map
constructor body: position -> scale -> angle -> color seed
```

共同 declaration order 已由当前源码精确表达，但四份成品的 concrete write 与异常边界不能
再概括成一条“所有 late members 失败都从 HM7 开始”的通用路径：

- Android old-libstdc++ 的 HM4–HM7 各自在构造期 eagerly 建立 11-bucket array；当前 HM 的
  bucket allocation/length path 失败时它仍是部分构造对象，不能执行完整 current-HM destructor；
- Android arm64 保留逐 HM 的 Itanium cleanup 阶梯，分别从当前 HM 之前最后一个完整成员开始；
- Android armv7 的 304-instruction Engine ctor 没有任何本地 member-cleanup landing，不能把
  Android arm64 或 iOS 的 prefix rollback 投射过去；
- iOS libc++ 的 HM4–HM7 构造全部是 zero/tag/load-factor stores，没有 allocation 或可抛 call；
  四次 seed helper 才是晚期 call sites，此时四个 HM 与三个 Variant 已全部完成，所以两端都
  从 HM7→HM6→HM5→HM4→三个 Variant 开始回滚；
- Android arm64 四次 seed setter 已内联成 delete/memcpy/标量写，HM7 完成后没有新的 C++
  throw call site，所以其 ctor cleanup 尾部恰好没有“完整 HM7 destructor”入口。

portable 源码无需行为改写：declaration-order 普通 member 构造会让目标 STL/编译器自然产生各自
边界。本轮只修正源码说明、旧报告和 recovery IDB 语义；没有手工 `reserve(10)`、没有伪造
Android bucket、也没有添加手动 rollback。

## 2. 四端入口与尾部字段映射

| 目标 | Engine ctor | ctor cleanup 形态 |
|---|---:|---|
| Android arm64-v8a | `0x67B76C` | ctor 尾部 Itanium landing blocks |
| Android armv7 | `0x560948` | 无本地 cleanup landing |
| iOS arm64 | `0x1001B7FB0` | 独立 cleanup body `0x1001B829C` |
| iOS armv7 | `0x1B7788` | SJLJ dispatcher `0x1B7B02` |

尾部字段的 ABI offset：

| member/region | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| raw wind owner | `+0x468` | `+0x234` | `+0x2F8` | `+0x17C` |
| five floats | `+0x470..+0x480` | `+0x238..+0x248` | `+0x300..+0x310` | `+0x180..+0x190` |
| eight bytes | `+0x484..+0x48B` | `+0x24C..+0x253` | `+0x314..+0x31B` | `+0x194..+0x19B` |
| five doubles | `+0x490..+0x4B0` | `+0x258..+0x278` | `+0x320..+0x340` | `+0x19C..+0x1C0` |
| labels-base Variant | `+0x4B8` | `+0x280` | `+0x348` | `+0x1C4` |
| labels Variant | `+0x4CC` | `+0x28C` | `+0x35C` | `+0x1D0` |
| frame-lists Variant | `+0x4E0` | `+0x298` | `+0x370` | `+0x1DC` |
| HM4 instant set | `+0x4F8` | `+0x2A4` | `+0x388` | `+0x1E8` |
| HM5 range map | `+0x530` | `+0x2C0` | `+0x3B0` | `+0x1FC` |
| HM6 var-ref map | `+0x568` | `+0x2DC` | `+0x3D8` | `+0x210` |
| HM7 value map | `+0x5A0` | `+0x2F8` | `+0x400` | `+0x224` |

64-bit Android 每个 old-libstdc++ HM 是 `0x38` bytes，32-bit Android 是 `0x1C`；iOS
libc++ 分别为 `0x28` 与 `0x14`。四个尾部容器正好填到各端 Engine 完整大小
`0x5D8/0x314/0x428/0x238`，没有隐藏的第五个 late container。

## 3. raw wind、cache、bytes、doubles 的 concrete writes

### 3.1 Android arm64

parts owner 在 `0x67BB14` 发布后：

- `0x67BB18/0x67BB1C` 两个 16-byte zero store 覆盖 `+0x468..+0x487`：raw wind、五个
  float cache 与前四个 bool；
- `0x67BB38` 在 `+0x488` 写 32-bit `1`，同时形成
  `selectorEnabled=1, queuing=0, dirty=0, debugPrint=0`；
- `0x67BB3C/0x67BB40/0x67BB30` 把五个 double 全写为 `1.0`；
- ABI alignment gap `+0x48C..+0x48F` 不是显式源码字段。

### 3.2 Android armv7

parts owner 在 `0x560B70` 发布，随后 `__aeabi_memclr4(+0x234, 0x1C)` 清 raw wind、五个
float 与前四个 bool；`0x560B9A` 向 `+0x250` 写 32-bit `1`，同样一次形成 selector true 与
queuing/dirty/debug false。五个 `STRD` 把 `+0x258..+0x278` 写为 double `1.0`；自然对齐 gap
`+0x254..+0x257` 没有独立写。

### 3.3 iOS arm64/armv7 的 dirty dead-store elimination

iOS arm64 在 `0x1001B8118..0x1001B8124` 清 raw wind、五个 float 与前四个 bool，之后只单独
写 selector `+0x318=1`、queuing `+0x319=0` 与 debug `+0x31B=0`。iOS armv7 的两次重叠
`VST1.32` 同样清到 `+0x197`，之后只写 `+0x198=1`、`+0x199=0`、`+0x19B=0`。

两份 iOS 成品都没有把 source-level `_dirty=false` 落物到 `+0x31A/+0x19A`；其第一条 concrete
dirty write 是 position seed 前的 true（arm64 `0x1001B81D0`，armv7 `0x1B7A32`）。因为该
false 在任何可观察读取前必被覆盖，optimizer 合法删除死写。Android 两端则借 selector 的
32-bit combined store 保留了 dirty zero。此前报告把四端都写成“先 concrete zero”过宽，本轮已
改为 source semantics 与 machine store 两层表述。

五个 double 在 iOS 两端仍全部 concrete 写成 `1.0`；这里不存在 optimizer omission。

## 4. 三个 Variant 的构造边界

四端都只需要为三份默认 `tTJSVariant` 建立 `Void` type tag：

| 目标 | 三个 concrete type-tag stores |
|---|---|
| Android A64 | `0x67BB44/0x67BB48/0x67BB4C` |
| Android A32 | `0x560B9E/0x560BA2/0x560BA6` |
| iOS A64 | `0x1001B8154/0x1001B815C/0x1001B8164` |
| iOS A32 | `+0x1CC/+0x1D8/+0x1E4` 的 zero stores，最后一项与 HM4 header 合并向量化 |

未被 Void tag 使用的 payload bytes 不能从这些 store 反推为三个额外 POD 字段。constructor
不建立 Array/Dictionary dispatch；那些 publication 属于 metadata/reset 路径。

## 5. Android old-libstdc++ HM4–HM7 eager construction

### 5.1 共同 11-bucket 初态

四个 container 都以 requested count `10` 进入 prime rehash policy，得到 `bucket_count=11`，
随后 `operator new(11*sizeof(pointer))`、zero bucket array，最后发布 bucket pointer。完整空表为：

```text
bucket_count = 11
bucket storage = 11 * sizeof(pointer)
global first node = null
size = 0
max_load_factor = 1.0f
```

Android arm64 的构造区间：

| member | header/policy | bucket new | bucket publish |
|---|---:|---:|---:|
| HM4 | `0x67BB50..0x67BB78` | `0x67BB84` | `0x67BBA4` |
| HM5 | `0x67BBA8..0x67BBD0` | `0x67BBDC` | `0x67BBFC` |
| HM6 | `0x67BC00..0x67BC28` | `0x67BC34` | `0x67BC58` |
| HM7 | `0x67BC5C..0x67BC84` | `0x67BC90` | `0x67BCA8` |

`bucket_count==1` 的 embedded-single-bucket branches 位于
`0x67BE68/0x67BE74/0x67BE80/0x67BE8C`；当前 hint=10 不走这些分支。size overflow 的四个
throw branches 位于 `0x67BEA8..0x67BEB4`。

Android armv7 保留四个 out-of-line helper call：

| member | Engine call | helper |
|---|---:|---:|
| HM4 | `0x560BBE` | `0x565842` |
| HM5 | `0x560BDC` | `0x564A16` |
| HM6 | `0x560BFA` | `0x564A6C` |
| HM7 | `0x560C16` | `0x564AC2` |

四个 helper 的 body 同形：先写 load factor/node/size，求 prime bucket count，分配并清零，最后
把 bucket pointer 写到 member 首 word。

### 5.2 Android arm64 的逐项 partial-member unwind

Android arm64 ctor 尾部的 no-predecessor landing fragments 精确编码“跳过当前 partial HM”：

| 当前失败 member | cleanup entry | 第一项完整析构 | 后续 |
|---|---:|---|---|
| HM4 | `0x67C154` | frame-lists Variant | labels、labels-base、owners、earlier prefix |
| HM5 | `0x67C0F0` | HM4 | Variants、owners、earlier prefix |
| HM6 | `0x67C0E0` | HM5 | HM4、Variants、owners、earlier prefix |
| HM7 | `0x67C07C` | HM6 | HM5、HM4、Variants、owners、earlier prefix |

`0x67C07C..0x67C0DC` 是 HM6 的完整 unordered destructor，`0x67C0E0` 调 HM5 dtor，
`0x67C0F0..0x67C150` 是 HM4 destructor，`0x67C154..0x67C170` 逆序析构三个 Variant。
随后从 parts owner 开始进入 V262 已闭合的 owner ladder。

这里最重要的负证据是：构造失败的 current HM 没有进入完整 destructor。它的 bucket pointer
尚未 publish，失败的 `operator new` 也没有可释放 allocation；只清理 declaration order 中更早的
完整 member。HM7 完成后的四段 seed 代码没有可抛 C++ call，因此 ctor EH tail 不需要 HM7 dtor。

### 5.3 Android armv7 没有本地 prefix cleanup

Android armv7 `0x560948` 共 304 条指令，正常 return 于 `0x560CAE`，之后只有 stack-check fail；
没有 Itanium landing、没有 SJLJ state，也没有 HM/Variant/owner destructor fragment。因而当前二进制
只能陈述：若四个 HM helper 或其他 callee 通过 unwind 抛出，该 Engine ctor 本身不回收已经完成的
member prefix。是否因该目标的编译选项/运行库把这些路径变成终止，不应靠其他端猜测；恢复报告只
记录成品边界。

## 6. iOS libc++ lazy construction 与 full-tail seed unwind

### 6.1 四个 HM 都是 non-throwing stores

iOS arm64：

- HM4 `0x1001B8168..0x1001B8180`；
- HM5 `0x1001B8184..0x1001B8198`；
- HM6 `0x1001B819C..0x1001B81B0`；
- HM7 `0x1001B81B4..0x1001B81C8`。

每项都只把 bucket pointer/count/first/size 清零并写 `max_load_factor=1.0f`。iOS armv7 将同样
写入向量化到 `0x1B79DC..0x1B7A28`，分别覆盖 HM4 `+0x1E8`、HM5 `+0x1FC`、HM6 `+0x210`、
HM7 `+0x224`。两端均无 bucket allocation、无 HM constructor call、无 partial-HM failure state。

### 6.2 seed calls 与共同完整尾部

| seed | iOS A64 call | iOS A32 stored call_site / call |
|---|---:|---:|
| position `{0,0}` | `0x1001B81E8` | `16` / `0x1B7A46` |
| scale `{1}` | `0x1001B8208` | `17` / `0x1B7A70` |
| angle `0` | `0x1001B8228` | `18` / `0x1B7A94` |
| color `{128,128,128,255}` | `0x1001B8258` | `19` / `0x1B7AC2` |

iOS arm64 的共同 cleanup entry `0x1001B8348` 依次调用：

```text
HM7 TtstrDouble map dtor
HM6 VarRef map dtor
HM5 VariableRange map dtor
HM4 ttstr set dtor
frame-lists Variant dtor
labels Variant dtor
labels-base Variant dtor
parts -> hair -> bust -> angle -> color -> scale -> position -> Player
earlier Engine prefix
_Unwind_Resume(original exception)
```

iOS armv7 dispatcher 以 stored `call_site-1` 分支；cases 15..18 全部落到 `0x1B7B54`，其
`0x1B7B5A..0x1B7B82` 给出完全相同的 HM7→HM4→Variant×3 前缀，然后从 parts owner 开始。
cleanup helper 自己再抛时继续进入编译器的 terminate/abort 边界，不尝试二次正常回滚。

因此 iOS arm64 recovery 注释中原有“late members or seed setters”过宽：late HM construction
本身没有 throw frontier；这个 full-tail entry 专属于四次 seed helper call（以及编译器可能合并到
同一 fully-constructed state 的等价 body throw），不能用来描述 Android eager-HM construction。

## 7. 与 portable 源码逐项比较

当前 `EmoteEngine.h` 声明严格按四端顺序排列，当前 `EmoteEngine.cpp` 的八个 direct owner 位于
member initializer list，四次 seed 位于 body 且顺序/常量相同。`std::unordered_*` 默认构造由
目标标准库决定，因此：

- libc++ Web/iOS 自然得到 lazy empty header；
- historical Android old-libstdc++ 的 eager bucket 是 ABI/库产物，不应由 portable 算法伪造；
- C++ declaration-order member unwind 自然排除尚未完成的 current member；
- 当前源码的 `_dirty=false` 保留 source semantics，同时允许 optimizer 删除死写。

本轮没有行为代码偏差。源码只新增 ABI-neutral 的 tail/unwind 注释，并把 `_dirty` 说明改为
Android concrete zero 与 iOS dead-store elimination 的精确区分。旧完整构造报告也已纠正：

1. owner constructor failure 不总是从 parts 开始；
2. Android armv7 不是“仅缺独立 Hex-Rays start”，而是没有本地 cleanup ladder；
3. iOS dirty 没有 concrete initial-zero store；
4. Android partial HM 与 iOS fully-constructed seed frontier 必须分开。

## 8. 边界与置信度

高置信度、四端直接机器证据：字段 offsets/order、concrete zero/one stores、Variant tags、HM
header sizes、Android 11 buckets、iOS lazy headers、seed order/constants、iOS full-tail destructor
order、Android arm64 partial-HM cleanup order、Android armv7 缺少本地 cleanup code。

保留边界：Android armv7 若实际发生跨 frame unwind 时的最终 runtime policy 还受该成品的全局
exception/terminate 配置控制；本轮不从其他 ABI 猜成“必然继续传播”或“必然 abort”。这不影响
`EmoteEngine` ctor 内部没有 prefix cleanup 的直接结论。

## 9. recovery IDB 写回

本轮共写回 **53 comments / 53 bookmarks / 8 semantic renames**：

- Android arm64：13 comments/bookmarks，覆盖 scalar/trigger/Variant、HM4–HM7 construct/commit、
  seed start 与四个 partial-HM cleanup entries；
- Android armv7：10 comments/bookmarks，覆盖无本地 EH、scalar/trigger/Variant、四个 HM helper 与
  seed frontier；另把三个仍为 `sub_xxx` 的 container helper 改成 `_guess` 语义名；
- iOS arm64：15 comments/bookmarks，覆盖 dirty dead-store、三个 Variant、四个 lazy HM、四个 seed
  calls 与 full-tail cleanup；把旧 `late members or seed setters` 注释原位改窄为 fully-constructed
  seed state；
- iOS armv7：15 comments/bookmarks，覆盖同一 concrete write、SJLJ call_site 16..19 与 cases
  15..18；另补五个 setter/container-dtor `_guess` 名。

新增 semantic names：

```text
Android armv7
0x565842  EmoteTtstrSet_ctor_bucket_hint_guess
0x564A16  EmoteVariableRangeMap_ctor_bucket_hint_guess
0x564A6C  EmoteVarRefMap_ctor_bucket_hint_guess

iOS armv7
0x1A418C  EmoteVarController_setTarget_guess
0x1A3798  EmoteAngleController_setTarget_guess
0x1B774C  EmoteTtstrSet_dtor_guess
0x1B7F84  EmoteVariableRangeMap_dtor_guess
0x1B7F48  EmoteVarRefMap_dtor_guess
```

iOS armv7 different-path 安全保存：

- pre-V263 backup：
  `out/idb-recovery/v263-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v263.i64`，
  377,731,280 bytes，SHA-256
  `53098CD827856C0483374F5692A7569D2B5439B01877B59D0CB99D703465AA81`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v263.i64`；
- `C:\IDA\idat.exe -A` candidate probe 退出 0；
- candidate/canonical resolved path 覆盖前均确认位于 workspace；
- candidate 与最终 canonical 均为 377,764,048 bytes，SHA-256
  `C0F26AC010CF85F2C48F99991D340BA29BAC35B150BDDF677D91BF41DC7C2CD5`；
- canonical 重新打开后回读 V263 constructor/dispatcher comments 与五个 semantic names，再关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,745,140 | `3505EB3E154A6039CEB320D48B2D131CDD3F2352A158EF77FD577343F10AFAB8` |
| Android armv7 | 345,903,517 | `22956DF4DDF4B2C951E60759B06B6615E16A7E9A768EF04E304FEDC08444B826` |
| iOS arm64 | 334,933,959 | `05AC48F65D3A16CBA1DF13F14B9CE9F525AA8456A145069605F2BADBF7633738` |
| iOS armv7 | 377,764,048 | `C0F26AC010CF85F2C48F99991D340BA29BAC35B150BDDF677D91BF41DC7C2CD5` |

最终 IDA MCP session 与本机 IDA process 均为 0。

## 10. 验证与产物

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 motionplayer test TU syntax-only 均退出 0；
  各只有仓库既有 `_tss` deprecated warning；
- Wasmtime Debug 完成 17-step rebuild/link；guest target 完成 1-step relink/exnref conversion；
- 首次并行调用两份 EMSDK shell 时，Web derived CMake cache 的 toolchain include 被竞争写为
  `/upstream/...`。随后改为单进程执行 `cmake --preset "Web Debug Config"`，配置成功并完成
  31-step Web rebuild/link；没有源码或 canonical reference 被该缓存故障修改；
- 修复后 Web、Wasmtime、guest 三目标再次构建均为 `ninja: no work to do`；
- 三份 Wasm section sizes 与 V261/V262 基线完全一致：

| artifact | size | SHA-256 | FUNCTION | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|
| Web `index.wasm` | 85,655,322 | `6039AA6D8DC48FB7CCC5840CFF7630EEE9838C1AB2809BCEE5B096BCD42EEC6F` | `0x1BD31` | `0x1A4109D` | `0x5A3E40` | `0x3185F7B` |
| Wasmtime `index.wasm` | 85,002,463 | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` | `0x1BA50` | `0x19E904B` | `0x5A1090` | `0x3141E11` |
| Wasmtime guest | 151,479,107 | `71551D4C11900AD438637BE4A30184156EFEBAEF4632B628068C7F307A30F2CA` | `0x1618E` | `0x13D7DCD` | `0x4D1630` | `0x1421EBA` |

Web/guest 因本轮被实际重新链接而 content hash 更新；Wasmtime 主产物 hash 保持 V261/V262
基线。三者 size 与全部关注 section size 均不变，本轮 compiled C++ 只含注释修正。
