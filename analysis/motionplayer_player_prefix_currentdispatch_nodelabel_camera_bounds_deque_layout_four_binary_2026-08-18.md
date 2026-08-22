# Player 前缀 currentDispatch / NodeLabelMap / camera / bounds / node deque 布局（V248，2026-08-18）

## 1. 结论

四份参考的完整 `Player` object 共享同一源码声明前缀：

```text
Player *rootPlayer
Player *parentPlayer
iTJSDispatch2 *currentDispatch        // non-owning
std::map<ttstr,int,utf16_less> nodeLabelMap
double cameraPosition[3]
double cameraTarget[3]
double stereovisionCamera[3]
float  cameraOffset[2]
double boundsMinX, boundsMinY, boundsMaxX, boundsMaxY
std::deque<MotionNode> nodes
```

各 ABI 中 map object 经后继 double 的自然对齐后立即到达 cameraPosition X，boundsMaxY 的末字节
都紧邻 node deque 的首字节；中间没有 `preview`、`priorDraw`、`outline`、draw region、Variant owner
或其他控制字段。Android armv7 的 24-byte tree 后有 4-byte alignment padding；其余 ABI 在该边界
无需 padding。

这纠正了当前 portable `Player.h` 中一个比 V247 更早的过时布局：旧声明把
`preview/priorDraw/chara/motion/outline` 放在两个 Player link 后，把 `currentDispatch`、raw-label map、
camera triples、bounds 和 node deque 散落到对象后部。它虽然可以维持大部分方法级行为，却反转了
map/deque 自动析构关系，也不可能复现四端共同 class prefix。

## 2. 四端完整 prefix offset 矩阵

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| rootPlayer | `+0x00` | `+0x00` | `+0x00` | `+0x00` |
| parentPlayer | `+0x08` | `+0x04` | `+0x08` | `+0x04` |
| currentDispatch | `+0x10` | `+0x08` | `+0x10` | `+0x08` |
| NodeLabelMap start | `+0x18` | `+0x0C` | `+0x18` | `+0x0C` |
| NodeLabelMap native size | `0x30` / 48 | `0x18` / 24 | `0x18` / 24 | `0x0C` / 12 |
| map start→camera aligned span | `0x30` / 48 | `0x1C` / 28 | `0x18` / 24 | `0x0C` / 12 |
| camera position X/Y/Z | `+0x48/+0x50/+0x58` | `+0x28/+0x30/+0x38` | `+0x30/+0x38/+0x40` | `+0x18/+0x20/+0x28` |
| camera target X/Y/Z | `+0x60/+0x68/+0x70` | `+0x40/+0x48/+0x50` | `+0x48/+0x50/+0x58` | `+0x30/+0x38/+0x40` |
| stereovision X/Y/Z | `+0x78/+0x80/+0x88` | `+0x58/+0x60/+0x68` | `+0x60/+0x68/+0x70` | `+0x48/+0x50/+0x58` |
| cameraOffset X/Y float | `+0x90/+0x94` | `+0x70/+0x74` | `+0x78/+0x7C` | `+0x60/+0x64` |
| bounds minX/minY/maxX/maxY | `+0x98/+0xA0/+0xA8/+0xB0` | `+0x78/+0x80/+0x88/+0x90` | `+0x80/+0x88/+0x90/+0x98` | `+0x68/+0x70/+0x78/+0x80` |
| MotionNode deque start | `+0xB8` / 184 | `+0x98` / 152 | `+0xA0` / 160 | `+0x88` / 136 |
| deque native object size | `0x50` / 80 | `0x28` / 40 | `0x30` / 48 | `0x18` / 24 |

两个关键恒等式在四端都成立：

```text
align_up(NodeLabelMap start + ABI map size, alignof(double)) == cameraPositionX offset
boundsMaxY offset + sizeof(double) == MotionNode deque offset
```

Android 两端是旧 libstdc++ `_Rb_tree` / deque header，iOS 两端是 libc++ `__tree` / deque。
24-byte Android armv7 map 加后继 double 所需的 4-byte padding，与 12-byte iOS armv7 map 不是
两个源码容器；它们都是同一 `std::map<ttstr,int,ttstr_utf16_less>` 的 ABI 投影。V248 初稿把
Android armv7 的 object size 与 aligned member span 都写成 28 bytes；V250 通过 constructor payload
clear 与后继 ramp-tree/live-evaluation 的同类边界纠正了这个表述。

## 3. 前三个 raw pointer 与 currentDispatch 生命周期

constructor 先发布：

```cpp
rootPlayer = this;
parentPlayer = nullptr;
currentDispatch = nullptr;
```

currentDispatch 的零写并不总与前两个 pointer 的 pair store 合并：

| 目标 | root/parent store | currentDispatch=null |
| --- | ---: | ---: |
| Android arm64 | `0x6CC150` | `0x6CC408` |
| Android armv7 | `0x5935E6` | `0x593774` |
| iOS arm64 | `0x10011EC30` | `0x10011EDC0` |
| iOS armv7 | `0x11D4B0..0x11D4B6` | `0x11D768` |

因此不能因为 constructor 前几条指令只写了两项，就把第三 word 解释为 map padding。play/progress
raw wrapper 会在参数转换/调用前把 objthis 原样写到第三 word，普通尾部再清零；它从不 AddRef/Release，
异常时可保留最后发布的 raw pointer。child Player 的 root/parent publication只覆盖前两项，不触碰
currentDispatch。

## 4. NodeLabelMap header 与相邻 camera block

constructor 在第三 pointer 后直接初始化 ordered raw-label map：

- Android arm64：map 起点 `+0x18`，header sentinel 写入覆盖 `+0x20..+0x47`；
- Android armv7：map 起点 `+0x0C`，24-byte object 的 non-empty payload 为 `+0x10..+0x23`，
  `+0x24..+0x27` 是 camera double 的 alignment padding；
- iOS arm64：map 起点 `+0x18`，libc++ lazy-zero tree 在 `+0x30` 前结束；
- iOS armv7：map 起点 `+0x0C`，12-byte tree 在 `+0x18` 前结束。

key/mapped node、null-backed empty 排序、duplicate last-index-wins 与 teardown 行为仍按既有
NodeLabelMap 纵切面：map 拥有 ttstr key backing，只保存 flat deque index，不拥有 MotionNode。

map 后的九个 double 在 constructor 中被精确清成 `+0.0`：

| 目标 | 首次/合并零写 | 后续重复零写（若有） |
| --- | ---: | ---: |
| Android arm64 | `0x6CC184`：`+0x48`, length `0x48` | `0x6CC500` |
| Android armv7 | `0x593608`：`+0x28`, length `0x48` | `0x59383E` |
| iOS arm64 | `0x10011EC58`：`+0x30`, length `0x48` | `0x10011EEA0` |
| iOS armv7 | `0x11D4DC..0x11D4FC`：SIMD/scalar stores | 无单一 memset |

72 bytes 正好是 9×8；前六项由 CameraNode 在 `cameraActive` gate 下发布，最后三项由
`setStereovisionCameraPosition` 直接写入。四个 stereovision setter 为：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6CD420` | `0x593F44` | `0x10011F54C` | `0x11E038` |

它们只写三个 double，不创建 Array/Dictionary/Variant，也不触碰 camera position/target。

## 5. cameraOffset：不属于九 double memset，但仍有独立 ctor 零写

连续九 double clear 在 cameraOffset 前恰好结束。V248 没有据此误判 offset 为未初始化字段，而是继续
扫描整个 constructor，找到四端分散的独立零写：

| 目标 | cameraOffset X/Y ctor zero |
| --- | ---: |
| Android arm64 | `0x6CC4E0`：一个 64-bit zero store 到 `+0x90/+0x94` |
| Android armv7 | `0x5937FA`：paired zero store 到 `+0x70/+0x74` |
| iOS arm64 | `0x10011EE54`：一个 64-bit zero store 到 `+0x78/+0x7C` |
| iOS armv7 | `0x11D842/0x11D846`：两个 scalar zero store 到 `+0x60/+0x64` |

所以 portable 的两个 `=0.0f` initializer 是正确行为，不能为了字面模拟九-double memset 而删除。
随后 `setCameraOffset` 的 double→float 窄化和 CameraNode 的 affine→int32→float 写入共享这两个字段；
post-prepare pass 始终读取该 pair 做 float translation。

## 6. bounds quartet 与 node deque 邻接

constructor 将 bounds 初始化为精确：

```cpp
{ +DBL_MAX, +DBL_MAX, -DBL_MAX, -DBL_MAX }
```

| 目标 | ctor sentinel commit | calcBounds reset |
| --- | ---: | ---: |
| Android arm64 | `0x6CC51C..0x6CC550` | `0x6C11C4..0x6C11C8` |
| Android armv7 | `0x593866..0x593878` | `0x58BE7C..0x58BE9C` |
| iOS arm64 | `0x10011EEC0..0x10011EEC8` | `0x100115CB8..0x100115CC0` |
| iOS armv7 | `0x11D910..0x11D92A` | `0x1135B8..0x1135D8` |

calcBounds 每次都重复同一 quartet，再从 node index 1 遍历；无贡献时保留 unordered sentinel。
紧随 maxY 的就是 native node deque：Android libstdc++ eager block/header 与 iOS libc++ lazy map/index
形态不同，但源码成员都必须是同一个 `std::deque<MotionNode>`。

把 `_nodes` 移回这里还修正自动析构顺序：declaration-order reverse destruction 先销毁 deque，随后才销毁
更早的 NodeLabelMap。Player 显式 destructor/reset 已在此之前释放非根 child/layer owners并清 map；
自动 deque destructor 最终销毁保留的 synthetic root。旧 portable 顺序把 map 放在 deque 后，导致自动
member order 与共同 native layout 相反。

## 7. preview/priorDraw 反证

V248 同时检查了旧 prefix 中的两个 Boolean。它们都不在 object 开头：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| preview | `+1092` | `+744` | `+980` | `+680` |
| cameraActive | `+1094` | `+746` | `+982` | `+682` |
| stereovisionActive | `+1095` | `+747` | `+983` | `+683` |
| priorDraw | `+1096` | `+748` | `+984` | `+684` |
| hasCamera | `+1100` | `+752` | `+988` | `+688` |

这些 offset 共同保持 `preview -> cameraActive -> stereovisionActive -> priorDraw -> hasCamera` 的相对
顺序，但 preview 与 cameraActive 间以及 priorDraw 与 hasCamera 间仍有尚未命名的 byte/padding 区。
portable 只迁移已知字段的声明顺序，不为未知 byte 伪造名称。

## 8. portable 源码修改

`cpp/plugins/motionplayer/Player.h`：

- 将 `_currentDispatch` 移到 `_rootPlayer/_parentPlayer` 后，恢复三个 raw-word prefix；
- 将 `_nodeLabelMap` 移到第三 word 后；
- 依次移动 camera position、camera target、stereovision camera、cameraOffset、bounds；
- 将 `_nodes` 移到 boundsMaxY 后，并从后部 variable-label cluster 删除旧声明；
- 从 object prefix 移走错误的 preview/priorDraw/chara/motion/outline；
- 将已知 Boolean 的相对声明改为 preview、cameraActive、stereovisionActive、priorDraw、hasCamera；
- 只写语义/相邻关系，不把四端绝对 offset 写入 compiled source comment。

没有重写方法算法，也没有新增脚本 surface。现有 camera、map、bounds、node-tree 与 raw-wrapper tests
继续验证相同可观察行为；这次主要修复 class declaration、自动 member lifecycle 和过时注释。

## 9. IDB 写回和 iOS armv7 安全保存

每份 IDB 写入 6 条 comment 和 6 个 bookmark，覆盖：九-double ctor clear、bounds→deque 边界、
stereovision direct writer、ctor bounds sentinels、calcBounds reset、cameraOffset 独立零写。总计
24 comments、24 bookmarks。iOS armv7 还将三个缺失身份恢复为：

- `Player_setStereovisionCameraPosition_guess`；
- `Player_calcBoundsRecursive_guess`；
- `Player_updateCameraNode_guess`。

私有 stripped identity 均保留 `_guess`。

iOS armv7 使用两阶段 different-path save：

- `out/idb-recovery/v248-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.v248.i64`；
- 最终 `Kirikiroid2_1.3.9_iOS_armv7.v248-final.i64`；
- 两份均为 376,805,584 bytes，最终 candidate 经独立 `idat.exe -A` probe 退出码 0；
- V247 canonical/loose files 保存到 `pre-v248-canonical/`；
- 中间 V248 canonical、canonical loose files 与 candidate loose files 保存到
  `pre-v248-field-final/`；
- 最终 candidate 与 canonical SHA-256 均为
  `C661F124926D389A3D9E13FC21A20774A59DB337B96761175882F7D85A7C5C82`；
- canonical MCP 回开成功读回 V247 两个名称和 V248 三个新名称；最终 session count 为 0。

四份最终 V248 IDB：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,656,687 | `292F5FF11F1F4C13FEED545DE3B54674DBEBF1F6B076AA76CF4D95744B1FB3BE` |
| Android armv7 | 345,626,147 | `88E31770CE6399965278343FA4094D6A78158E41FCA25244F1ECFDA1A4B0E006` |
| iOS arm64 | 334,615,809 | `E3877EB8E5091BF456867DB6A2EEFDAA950B1147340E5ED930D48F089FB60C1A` |
| iOS armv7 | 376,805,584 | `C661F124926D389A3D9E13FC21A20774A59DB337B96761175882F7D85A7C5C82` |

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
| Web `index.wasm` | 85,655,362 | `0x1BD31` | `0x1A410C5` | `0x5A3E40` | `0x3185F7B` | `C1DEF7CAA89EE55BA195755D512C05FCEA114AF505099F5DA6C19DFCD2CEF941` |
| Wasmtime `index.wasm` | 85,002,503 | `0x1BA50` | `0x19E9073` | `0x5A1090` | `0x3141E11` | `99EE892F7D7C3CE94B2626C0745099B1C7192A00ECE3DC0E546C282F2315C784` |
| Wasmtime guest | 151,478,361 | `0x1618E` | `0x13D7DF3` | `0x4D1630` | `0x1421EBA` | `BC646BEA91068EC16883C07EE3960C30C1C1F3EAA99FE1A30271A1C3587D7FDF` |

相对 V247，Web/Wasmtime 主模块各增加 311 bytes，且增量全部落在 CODE；FUNCTION、DATA、name
均不变。这是大量 Player field address 与 map/deque reverse-destruction 代码生成改变的预期结果，不是
脚本成员、静态数据或资源漂移。guest 含完整测试 TU/debug custom sections，不能要求总大小与主模块
CODE 等量变化。

## 11. V249 follow-up

V249 已闭合 node deque 的直接后继：HM1、HM2、selected parameter raw pointer、parameter vector 与
ramp multimap，以及正常析构和 constructor-failure 的逆序回滚。证据和源码迁移见
`analysis/motionplayer_player_post_node_hm1_hm2_parameter_container_layout_four_binary_2026-08-18.md`。

V250 从 ramp multimap 末端继续恢复紧邻的 raw numeric/control block 和首个 Variant owner；未识别间隙
仍只记录 ABI offset/类型事实，不根据 portable 旧顺序命名。
