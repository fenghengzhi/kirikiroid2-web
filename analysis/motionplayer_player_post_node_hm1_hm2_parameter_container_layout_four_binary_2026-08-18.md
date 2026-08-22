# Player node-deque 后 HM1 / HM2 / parameter containers 布局与生命周期（V249，2026-08-18）

## 1. 结论

V248 闭合到 `std::deque<MotionNode> nodes` 后，四份参考继续给出完全一致的源码成员序列：

```text
std::deque<MotionNode> nodes
EvalCascadeMap          evalCascadeMap       // HM1: ttstr -> EvalCascadeState
LabelValueMap           evalResultValues     // HM2: raw-label ttstr -> double
MotionParameterEntry   *selectedParameterEntry   // raw, non-owning
vector<MotionParameterEntry> parameterEntries
ParameterRampMap        parameterRampMap     // multimap<ttstr, MotionParameterEntry*>
```

这里的“完全一致”指声明顺序、容器类别、ABI header 边界、正常析构顺序和 constructor failure
回滚顺序都能在四端互相解释。Android 两端是旧 libstdc++，iOS 两端是 libc++，所以相同源码容器的
header 大小与空容器初始化策略不同；不能把这些 ABI 差异解释成不同成员。

这修正了 portable `Player.h` 的另一组旧布局假设：旧声明把 HM1 放在 class-static/headless 状态后，
把 HM2 放到 HM3/HM4 后，又把 parameter vector、ramp map 和 selected pointer 放在 event vector 后，
而且 selected pointer 排在两个 owning containers 之后。方法级数据流多数仍能工作，但对象物理布局、
自动 member lifecycle 与异常回滚都不可能匹配参考。

## 2. 四端 offset 与 native object size

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| MotionNode deque start | `+0xB8` | `+0x98` | `+0xA0` | `+0x88` |
| deque native size | `0x50` / 80 | `0x28` / 40 | `0x30` / 48 | `0x18` / 24 |
| HM1 start | `+0x108` | `+0xC0` | `+0xD0` | `+0xA0` |
| HM1 unordered-map header size | `0x38` / 56 | `0x1C` / 28 | `0x28` / 40 | `0x14` / 20 |
| HM2 start | `+0x140` | `+0xDC` | `+0xF8` | `+0xB4` |
| HM2 unordered-map header size | `0x38` / 56 | `0x1C` / 28 | `0x28` / 40 | `0x14` / 20 |
| selectedParameterEntry | `+0x178` | `+0xF8` | `+0x120` | `+0xC8` |
| parameterEntries vector start | `+0x180` | `+0xFC` | `+0x128` | `+0xCC` |
| vector native size | `0x18` / 24 | `0x0C` / 12 | `0x18` / 24 | `0x0C` / 12 |
| parameterRampMap start | `+0x198` | `+0x108` | `+0x140` | `+0xD8` |
| multimap native size | `0x30` / 48 | `0x18` / 24 | `0x18` / 24 | `0x0C` / 12 |
| first byte after ramp map | `+0x1C8` | `+0x120` | `+0x158` | `+0xE4` |

四端同时满足：

```text
nodes + sizeof(native deque) == HM1
HM1  + sizeof(native unordered_map) == HM2
HM2  + sizeof(native unordered_map) == selectedParameterEntry
selectedParameterEntry + sizeof(pointer), aligned == parameterEntries
parameterEntries + sizeof(native vector) == parameterRampMap
```

64 位 selected pointer 后不需要额外 padding；32 位 vector 同样从 pointer 的下一 word 开始。
Android armv7 的 ordered-tree object 从 `+0x108` 占 24 bytes 到 `+0x120`；构造器从
`+0x10C` 开始清 20-byte non-empty payload，未写的首 word 属于 empty comparator/base，不是把
object 延长到 `+0x124` 的 header。V249 初稿曾把这条 payload 形状误记为 28-byte object，V250
通过紧邻 `+0x120` 的 live-evaluation 读写纠正。
因此这不是“几个容器在大致相同区域”的推测，而是一条没有未解释 member gap 的连续物理边界。

## 3. constructor：声明顺序与优化后 store 顺序的区别

### 3.1 Android arm64

- `0x6CC198..0x6CC1F0`：紧接 deque 的 HM1 header/default bucket state；
- `0x6CC1E8..0x6CC248`：独立初始化 HM2；
- `0x6CC264` 起：vector 三指针与后继 multimap tree header；
- `0x6CC544`：较晚才把 selected raw pointer 清零。

### 3.2 Android armv7

- `0x593614..0x593630`：HM1；
- `0x593638..0x593650`：HM2；
- `0x593658` 起：vector 与 ramp multimap；
- `0x59387C`：较晚清 selected raw pointer。

Android 旧 libstdc++ 的两个 unordered maps 都建立默认 11-bucket 表，并各自保有 bucket pointer、
bucket count、before-begin/first-node、element count、rehash policy 和 single-bucket fallback 等 ABI 状态。
两个 allocation/initialization path 是独立的；HM2 不是 HM1 的尾部数组或 secondary view。

### 3.3 iOS arm64

- `0x10011EC60..0x10011EC68` 从 `Player+0xA0` 做 0x50-byte clear：前 0x30 bytes 是 deque，
  后 0x20 bytes 已进入 HM1；`0x10011EC70` 再把 HM1 max-load-factor 写为 `1.0f`；
- `0x10011EC78..0x10011EC84` 初始化 HM2，并写独立的 `1.0f` load factor；
- `0x10011EC88..0x10011EC98` 连续初始化 vector 与 libc++ multimap sentinel；
- `0x10011EECC` 才清 selected raw pointer。

### 3.4 iOS armv7

- `0x11D504..0x11D516` 的 SIMD/scalar zero stores 跨过 deque→HM1 边界，随后写 HM1 `1.0f`；
- `0x11D524/0x11D528` 初始化 HM2 与它自己的 `1.0f`；
- `0x11D532..0x11D55A` 初始化 vector 和 ramp multimap；
- `0x11D936` 较晚清 selected raw pointer。

iOS libc++ unordered-map 默认构造是 lazy empty table：bucket storage 保持 null/zero，直到第一次插入；
40-byte/20-byte header 仍包含各自的 bucket-list、first-node、size 和 load-factor compressed state。
它与 Android eager 11-bucket 策略是标准库 ABI 差异，不改变两个 `std::unordered_map` 的源码身份。

selected pointer 的零写在四端都可能被 optimizer 调度到 container 初始化很久以后。它的成员位置由
offset 连续性、所有直接读写和“析构时没有对应 destructor call”共同确定，不能按 constructor 指令
时间顺序把它误放到 ramp map 后。

## 4. 两个 unordered maps 的角色与内部 ownership

### HM1：EvalCascadeMap

HM1 的 key 是 UTF-16 `ttstr` cascade key，hash 使用项目已恢复的 cached UTF-16 hash。mapped
`EvalCascadeState` 拥有自己的 key/chain/vector backing，并保存匹配 child nodes 的 non-owning pointer
cache。它不拥有 `MotionNode`；因此 node deque 必须先于 HM1 构造、晚于 HM1 析构，才能让缓存 pointer
在 HM1 的整个有效期内指向仍存在的 deque elements。

### HM2：LabelValueMap

HM2 是 raw PSB label `ttstr -> double`。binder 在 HM1 cascade propagation 后写入 HM2；miss path
CopyRef key 并 value-initialize mapped double，再由 caller 覆写结果。它与 Engine variable map、join
variable snapshot 使用相同 specialization，但每个 owner 都有独立 native header、bucket chain 和节点。

两个 maps 都是链式 standard-library unordered containers，不是 open addressing。Android/iOS bucket
header 与 node ABI 不同，但 key ownership、duplicate-key update 和 clear/dtor 语义保持同源。

## 5. parameter pointer / vector / multimap 数据流

`parameterEntries` 是 parse/append 的唯一 owning table，也是 idle `frameProgress` gate 检查的同一 vector。
`selectedParameterEntry` 只在 motion content 的 parameterize 选择成功后指向 vector element；它不拥有
fallback record，也不在析构时 delete/release。

`finalizeParameterTable_guess` 在参数表稳定后建立 `parameterRampMap`：

```text
multimap key    = parameter id ttstr
multimap value  = MotionParameterEntry* pointing into parameterEntries
```

equal-range ramp loop 通过这些 pointer 原地更新 vector record，node evaluation 读取的也是同一 record。
因此 pointer 有效期边界很严格：先完成可能 reallocate 的 vector population，再发布 selected/ramp aliases；
清理时先撤销 ramp aliases，再释放 vector elements/backing。任意在 publication 后继续扩容 vector 的实现
都会使 selected/ramp pointers 悬空，不符合参考数据流。

ramp map 的 tree nodes 拥有 `ttstr` key backing，但 mapped value 只是 raw pointer。selected pointer 也只是
raw alias；两者都不延长 `MotionParameterEntry` 的生命周期。

## 6. normal destruction：显式 purge 与自动 member teardown

Player destructor 前段先执行语义清理：

1. `purgeParameterRampMap_guess` 从本 Player/ancestor registrations 撤销 parameter pointers；
2. 从 vector 尾向头析构 `MotionParameterEntry`，持续回退 live end；
3. reset/release old node tree、render owners 与其余 Player state；
4. 进入 compiler-generated reverse member teardown。

最后一段在四端的调用顺序如下：

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| ramp multimap dtor | `0x6CD124` | `0x593D54` | `0x10011F3E8` | `0x11DE80` |
| parameter vector backing dtor/free | `0x6CD130` 起 | `0x593D5C` | `0x10011F3F4` | `0x11DE8E` |
| HM2 dtor | `0x6CD164` | `0x593D62` | `0x10011F3FC` | `0x11DE94` |
| HM1 dtor | `0x6CD1B8` | `0x593D6A` | `0x10011F404` | `0x11DE9C` |
| MotionNode deque dtor | `0x6CD20C` | `0x593D72` | `0x10011F40C` | `0x11DEA4` |
| NodeLabelMap dtor | `0x6CD214` | `0x593D78` | `0x10011F414` | `0x11DEAA` |

selected pointer 在这条序列中没有 owner destructor。显式 parameter-entry loop 已把 vector live range 清空，
后面的 vector destructor 主要释放 backing；显式 purge 已撤销 aliases，后面的 empty ramp-tree destructor
完成 native header/tree cleanup。这种“两阶段清理”解释了为什么不能只看 destructor 最后几条 call。

## 7. constructor failure 与边界行为

iOS armv7 的 SJLJ landing pad `0x11DA22` 把 constructor call-site state 分流到同一逆序 cleanup tail。
当后续 Variant/owner construction 抛出时，`0x11DB66..0x11DB92` 明确执行：

```text
parameterRampMap
parameterEntries
HM2
HM1
MotionNode deque
NodeLabelMap
resume unwind
```

Android 两端的 eager unordered-map allocation 还暴露更早的部分构造边界：若 HM2 默认 bucket allocation
失败，已完成的 HM1 和 deque/map prefix 会被回滚；若更晚 owner construction 失败，则 ramp/vector/HM2/HM1/
deque/map 按已构造集合逆序回滚。iOS lazy unordered maps 在空构造时通常不分配 bucket array，但生成的
member cleanup 仍保持相同声明顺序。

这给出两个重要边界：

- native object 从不在 HM2 failure 后泄漏 HM1 bucket/tree state；
- raw selected pointer 即使已经清零/写入，也不会参与 unwind owner count，更不会 delete vector element。

## 8. portable 源码修改

`cpp/plugins/motionplayer/Player.h` 已改为：

- 在 `_nodes` 后直接声明 `_evalCascadeMap` 与 `_evalResultValues`；
- 紧接着声明 non-owning `_selectedParameterEntry`、owning `_parameterEntries` 与 `_parameterRampMap`；
- 从 event state 删除旧 parameter declarations；
- 从 class-static/HM3/HM4 区删除旧 HM1/HM2 declarations；
- 把 container role、alias lifetime 和 reverse teardown 写成语义注释，不把平台绝对 offset 写入 compiled
  source。

没有改写 parse、binder、evaluation 或 ramp 算法，也没有新增 script surface。这个修改主要恢复对象
结构和 C++ 自动生命周期；现有方法继续访问同名 field，但编译器现在按参考顺序生成 constructor/dtor/
unwind code。

## 9. IDB 写回与 iOS armv7 安全保存

四份 IDB 各写入 6 条 comment 和 6 个 bookmark，共 24 comments、24 bookmarks，覆盖：

- deque→HM1 边界；
- HM2 独立 header；
- selected/vector/ramp 连续布局；
- ramp→vector reverse destruction；
- HM2→HM1 reverse destruction；
- maps→deque→NodeLabelMap destruction tail。

本轮不新增未经证实的 private function identity，因此没有 rename；既有 `_guess` 名保持不变。

iOS armv7 继续使用 different-path packed save：

- V248 canonical 备份为
  `out/idb-recovery/v249-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v249.i64`；
- V249 candidate 为同目录
  `Kirikiroid2_1.3.9_iOS_armv7.v249.i64`；
- candidate 经独立 `C:\IDA\idat.exe -A` probe，退出码 0；
- 旧 canonical loose `id0/id1/nam` 移入 `pre-v249-canonical-loose/`，旧 `id2` 另有可恢复 copy；
- candidate 安装为 canonical 后，经 MCP reopen 读回三处 V249 constructor comments、
  `Player_ctor_guess`/`Player_dtor_guess` 和 V248 的三个 camera/bounds `_guess` 名；
- candidate 与 canonical 均为 376,838,352 bytes，SHA-256
  `CEAC32DDE1B928203BF83B7DC15DB61514775E440E5CC5E135B0E99339B727B4`。

四份最终 V249 IDB：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,656,687 | `0B7F84A780AE93BB3F11DD3543C1E35E0E976915799E41FC9E9184844288C63B` |
| Android armv7 | 345,626,147 | `D973898AF53B6C414E712AAA9369304ABF2C379379D8B99A503F5C5CD025883F` |
| iOS arm64 | 334,624,001 | `E9D5DED547F83F59B4FF90DA60CA58766907E7CF8A6B5B870A01AB4392572E45` |
| iOS armv7 | 376,838,352 | `CEAC32DDE1B928203BF83B7DC15DB61514775E440E5CC5E135B0E99339B727B4` |

## 10. 验证与最终 wasm 基线

- complete motionplayer Catch2 TU ordinary/headless syntax：通过，仅既有 `_tss` warning；
- Web：33-step affected rebuild 通过；
- Wasmtime：62-step main/guest-object rebuild 通过；
- `krkr2_wasmtime_guest`：通过并完成 exnref 转换；
- 三条 build 命令复验均为 `ninja: no work to do.`；
- scoped `git diff --check`：无 whitespace error，仅工作树既有 LF/CRLF 提示；
- IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,362 | `0x1BD31` | `0x1A410C5` | `0x5A3E40` | `0x3185F7B` | `86DEEC0EBEC23334E8528DCE2C7C73CF7850E8422E925562598DC5F9DC4CB411` |
| Wasmtime `index.wasm` | 85,002,503 | `0x1BA50` | `0x19E9073` | `0x5A1090` | `0x3141E11` | `5349CAAE5B9423C4673B43BCEF520A2B792B3DBED7E1D362501C8C98302FECC6` |
| Wasmtime guest | 151,478,367 | `0x1618E` | `0x13D7DEB` | `0x4D1630` | `0x1421EBA` | `253ADD8B9C4FF026650B44D0DAD6D37BC56396327F1898F70BD31EDCEB63EFDF` |

相对 V248，Web/Wasmtime 主模块的总大小与列出的 FUNCTION/CODE/DATA/name section size 都不变，
但 SHA-256 改变，说明 field displacement 与 cleanup call ordering 在等长代码中发生了实际变化。
guest 总大小增加 6 bytes，CODE 减少 8 bytes；FUNCTION/DATA/name 不变，其余 14-byte 净变化来自
未列的小节/自定义调试 metadata，而不是 script member、静态数据或资源 surface 增删。

## 11. V250 follow-up

V250 已闭合 ramp map 后的 evaluation/emoteAngle/cameraAngle、四 frame-state bytes、type-1 owners、
priority/root cursor/times、delta/damping/control bytes、internal-ready/needs-assign pair 与 rootContent。
证据和源码迁移见
`analysis/motionplayer_player_post_ramp_frame_core_type1_root_owner_layout_four_binary_2026-08-18.md`。

V251 从 rootContent 后的 find-source ResourceManager Variant 开始，继续恢复 source workspace 与其
constructor/destructor/unwind；当前临时 `chara/motion/outline` 不作为直接后继假设。
