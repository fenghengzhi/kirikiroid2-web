# MotionPlayer Player tag 后标量/控制连续区四参考闭环（V255，2026-08-18）

## 1. 结论

本轮以 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考为共同真值，从 V254 已闭合的 `tagFrameSource` Variant 尾端继续
追踪 Player 物理布局。四端共同恢复出一个没有插入其他 source member 的连续区：

```text
tagFrameSource Variant
preview
syncActive
cameraActive
stereovisionActive
priorDraw
independentLayerInherit
syncWaiting
playing
cameraAlive
<natural padding>
cameraFov
zFactor
frameTickCount
frameLastTime / cached lastTime
frameLoopTime / loopTime
completionType
maskMode
processedMeshVerticesNum
colorWeight
outsideFactor
speed
meshDivisionRatio
```

关键更正有三项：

1. 九个 Boolean byte 的声明顺序可以由四端构造 store 和各独立访问器完全闭合；
   `_independentLayerInherit` 位于 `priorDraw` 与 `syncWaiting` 之间，不在 colorWeight
   后面。
2. 构造器把 `zFactor` 初始化为精确 `+0.0`，不是本地旧值 `1.0`。arm64 Android
   的一个 128-bit constant store 同时写 `{cameraFov=0.2, zFactor=0.0}`，其余三端
   以独立 word/double store 给出相同值。
3. 四个完整构造体都初始化 `frameTickCount`，但都不写紧随其后的 `lastTime` 与
   `loopTime` 两个 double。它们没有可靠的构造默认值；普通 motion 初始化随后从
   motion content 提交 `loopTime`、`lastTime`。把 declaration initializer 写成零会
   虚构原版不存在的确定性边界。

旧 `libkrkr2.so` 注释没有用于证明字段身份；本轮重新读取四个当前构造器、直接
getter/setter 与 producer/consumer 地址。

## 2. 精确四 ABI 布局

`tTJSVariant` 在 64 位目标占 20 bytes，在 32 位目标占 12 bytes。V254 的 tag owner
结束地址恰好等于本轮第一个 Boolean 的地址：

| 目标 | tag owner | Variant 尾端 / `preview` 起点 |
|---|---:|---:|
| Android arm64 | `+0x430` | `+0x444` |
| Android armv7 | `+0x2DC` | `+0x2E8` |
| iOS arm64 | `+0x3C0` | `+0x3D4` |
| iOS armv7 | `+0x29C` | `+0x2A8` |

完整连续表：

| source member / 物理槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `preview` | `+0x444` | `+0x2E8` | `+0x3D4` | `+0x2A8` |
| `syncActive` | `+0x445` | `+0x2E9` | `+0x3D5` | `+0x2A9` |
| `cameraActive` | `+0x446` | `+0x2EA` | `+0x3D6` | `+0x2AA` |
| `stereovisionActive` | `+0x447` | `+0x2EB` | `+0x3D7` | `+0x2AB` |
| `priorDraw` | `+0x448` | `+0x2EC` | `+0x3D8` | `+0x2AC` |
| `independentLayerInherit` | `+0x449` | `+0x2ED` | `+0x3D9` | `+0x2AD` |
| `syncWaiting` | `+0x44A` | `+0x2EE` | `+0x3DA` | `+0x2AE` |
| `playing` / local `_allplaying` | `+0x44B` | `+0x2EF` | `+0x3DB` | `+0x2AF` |
| `cameraAlive` frame byte | `+0x44C` | `+0x2F0` | `+0x3DC` | `+0x2B0` |
| natural padding | `+0x44D..44F` | `+0x2F1..2F7` | `+0x3DD..3DF` | `+0x2B1..2B3` |
| `cameraFov` | `+0x450` | `+0x2F8` | `+0x3E0` | `+0x2B4` |
| `zFactor` | `+0x458` | `+0x300` | `+0x3E8` | `+0x2BC` |
| raw `frameTickCount` | `+0x460` | `+0x308` | `+0x3F0` | `+0x2C4` |
| cached motion `lastTime` | `+0x468` | `+0x310` | `+0x3F8` | `+0x2CC` |
| motion `loopTime` | `+0x470` | `+0x318` | `+0x400` | `+0x2D4` |
| signed `completionType` | `+0x478` | `+0x320` | `+0x408` | `+0x2DC` |
| signed `maskMode` | `+0x47C` | `+0x324` | `+0x40C` | `+0x2E0` |
| uint32 processed count | `+0x480` | `+0x328` | `+0x410` | `+0x2E4` |
| packed render color | `+0x484` | `+0x32C` | `+0x414` | `+0x2E8` |
| `outsideFactor` | `+0x488` | `+0x330` | `+0x418` | `+0x2EC` |
| speed multiplier | `+0x490` | `+0x338` | `+0x420` | `+0x2F4` |
| `meshDivisionRatio` | `+0x498` | `+0x340` | `+0x428` | `+0x2FC` |
| 下一物理 member 起点 | `+0x4A0` | `+0x348` | `+0x430` | `+0x304` |

Android arm64、Android armv7、iOS arm64 都将 double 按 8 bytes 对齐；iOS armv7
在 class layout 内只要求 4-byte double alignment，所以 `cameraFov` 从 `+0x2B4`
开始。这是 ABI 自然 padding，不是 source-level padding 数组。

## 3. 构造器证据与精确默认值

四端构造函数：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |

共同构造结果：

| member | 构造行为 |
|---|---|
| `preview` | false |
| `syncActive` | 从 process-global `defaultSyncActive` byte 复制一次；四个 image 初值均 false |
| camera/stereovision/prior/independent/waiting/playing/alive | 全部 false |
| `cameraFov` | exact double `0.2` (`0x3FC999999999999A`) |
| `zFactor` | exact double `+0.0` |
| `frameTickCount` | exact double `+0.0` |
| cached `lastTime` | **不写** |
| `loopTime` | **不写** |
| `completionType` / `maskMode` | signed Int32 zero |
| processed count | uint32 zero |
| internal packed color | `0xFF808080` |
| outside/speed/mesh | exact `1.5 / 1.0 / 1.0` |

### 3.1 九 byte store 形状

Android arm64：

- `0x6CC484` 清 preview；
- `0x6CC4FC` 写 global default 到 syncActive；
- `0x6CC4EC` 的 halfword clear 覆盖 camera/stereovision；
- `0x6CC4D4` 的 halfword clear 覆盖 prior/independent；
- `0x6CC488` 的 halfword clear 覆盖 waiting/playing；
- `0x6CC4D8` 清 cameraAlive。

iOS arm64 采用相同配对思想，但 prior 与 independent 分别清零：`0x10011EE70`
清 camera/stereo，`0x10011EE30` 清 prior，`0x10011EE88` 清 independent，
`0x10011EE10` 清 waiting/playing。两个 32 位目标把多数 byte store 展开为独立指令，
进一步排除了 halfword 注释误配字段的可能。

### 3.2 FOV / zFactor

- Android arm64 `0x6CC4F8` 把 rodata `0x14D46C0` 的 16 bytes 写到
  `+0x450`；原始字节是 `9A 99 99 99 99 99 C9 3F 00 ... 00`，即 `{0.2, 0.0}`。
- Android armv7 `0x593812` 组装并写 `0.2`，`0x59381E` 清 `zFactor`。
- iOS arm64 `0x10011EE84` 写 literal `0x3FC999999999999A`，
  `0x10011EE8C` 清后一 double。
- iOS armv7 `0x11D882/0x11D88E` 写 `0.2` 的高低 word，
  `0x11D89A/0x11D89E` 清 `zFactor` 两 word。

因此旧本地 `_zFactor = 1.0` 是确定错误；它还会改变 `setZFactor(0.0)` 是否 dirty、
新 child 未被递归 visitor 命中时的保留值，以及 CameraNode/shape Z 投影。

### 3.3 intentionally-uninitialized motion metadata

四个完整 constructor instruction range 中，`frameTickCount` 均有明确零写，但
`lastTime/loopTime` 两个 displacement 均没有任何 store。直接 getter 分别为：

| 目标 | frameLastTime getter | frameLoopTime getter |
|---|---:|---:|
| Android arm64 | `0x6D6B84` | `0x6D6B8C` |
| Android armv7 | `0x598FF6` | `0x599000` |
| iOS arm64 | `0x1001256D8` | `0x1001256E0` |
| iOS armv7 | `0x1248F8` | `0x124902` |

getter 都只是 raw double load，没有“尚未加载则返回零”的 guard。ordinary motion init
按 `loopTime -> lastTime -> tag -> priority/root` 读取/提交。因此正常成功加载后两槽有效；
在此前直接查询时，参考实现没有稳定零默认，可能暴露 allocator/stack 残留位型。端口现按
声明不提供 initializer，保留这个边界，不把旧本地安全化行为冒充原版语义。

## 4. 属性、producer 与边界分离

连续布局不表示这些 byte 共享语义：

- `syncActive` 可读写；`syncWaiting` 只读，由 timeline steppers 置位；`playing`
  是本地播放状态；三者相邻但没有合并状态机。
- `cameraActive` 门控 CameraNode 的 FOV/position/target 发布，
  `stereovisionActive` 门控更晚的 perspective pass，`cameraAlive` 是每帧 active
  type-5 结果。三者互不替代。
- `priorDraw` 公开 setter 真正写 byte；`independentLayerInherit` 的公开 setter 在值
  不等时只遍历节点标 dirty，不写 byte，真正非构造提交来自 type-3 child 初始化。
- `completionType`、`maskMode` 保留完整 signed Int32；processed count 是 uint32
  wraparound 聚合；colorWeight 在脚本边界交换 byte0/byte2。相邻 32-bit slot 不应被
  折叠成一个 flags word。
- outside/speed/mesh 都是无验证 raw-double property；NaN、无穷和 signed zero 原样保留。

## 5. 本地源码和测试落地

`Player.h` 已把上述完整区块直接放在 `_tagFrameSourceVariant` 后，删除原先散落在
后部的重复声明，并恢复：

- 九 Boolean 精确顺序；
- `cameraFov -> zFactor -> frameTick -> last -> loop`；
- `completion -> mask -> processed -> color`；
- `outside -> speed -> mesh`；
- `_zFactor = 0.0`；
- last/loop 无 declaration initializer。

源码注释只记录四端共同语义，不写任何目标绝对地址。unit translation unit 同步把未被
type-4 visitor 命中的第二 child 默认 zFactor 从 `1.0` 改为 `0.0`，并锁定 fresh Player
的 `+0.0` 与 `setZFactor(0.0)` equal/no-dirty 边界。

## 6. IDB 写回

四库总计写回 68 comments、29 bookmarks。前三库已有本轮公开访问器语义名。iOS armv7
当前 recovery 库仍有一批早期报告已确认但保存时遗失的 `sub_*`，本轮 fresh disasm
后恢复 34 个 `_guess` 名称，包括：

- completion/preview/prior/outside/mesh/speed/sync get/set；
- frameTick、cameraActive/stereovisionActive get/set；
- mask get/set、independent get/set；
- rootTransform、zFactor get 与 zFactor set；
- cameraFOV、cameraAlive、playing、syncWaiting；
- colorWeight get/set 与 defaultSyncActive get/set。

iOS armv7 继续使用 different-path 安全保存：

- pre-V255 backup：
  `out/idb-recovery/v255-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v255.i64`，
  377,411,792 bytes，SHA-256
  `92DB6DE68F589736AD136540BC04D1654454FE0B87CC0B0033197B3968A8F93A`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v255.i64`；
- `C:\IDA\idat.exe -A` 独立 probe 退出 0；
- canonical loose working files 分别移到 `pre-v255-canonical-loose/` 与
  `verify-readback-loose/`，没有删除；
- candidate 替换 canonical 后重新由 MCP 打开，回读 36 个相关 semantic names 和
  constructor 的 20 条 V255 comment，随后关闭且保持零 session。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,581,258 | `8FEAEA05F9C6A090BAC3E827AE034F131346B7B7D9EB4E46D211EB8031332520` |
| Android armv7 | 345,780,595 | `56CAFDF93460B8F02A561E0987E7A383D50857404CDF84CAEA8FC28451633F7F` |
| iOS arm64 | 334,786,470 | `0E54AC3681D5A41B545767BACF1872B01030965586BCB526771ED9DDCA3B5CC5` |
| iOS armv7 | 377,493,712 | `07E8CBF43E988AE0C4C6F57E675F8DDF0F2315504AD7B6CD5BFBE9E02B221592` |

## 7. 验证与 wasm 基线

实际完成：

- 完整 `motionplayer-dll.cpp` 普通 Web `-fsyntax-only`：通过；
- 同一完整 TU 加 `KRKR2_WASMTIME_HEADLESS=1`：通过；
- Web Debug：33-step rebuild/link 通过；
- Wasmtime Headless Debug：62-step rebuild/link 通过；
- `krkr2_wasmtime_guest`：2-step rebuild/link/exnref conversion 通过；
- 三目标串行复核均为 `ninja: no work to do`；
- `git diff --check` 无 whitespace error，仅工作树既有 LF/CRLF warning；
- IDA MCP session 数为 0。

产物：

| wasm | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `index.wasm` | 85,655,346 | `0x1BD31` | `0x1A410B5` | `0x5A3E40` | `0x3185F7B` | `069C605A5DD5114A42A700CC8D8F48852CD2CEDEC2751A06010D3E7FC8666D23` |
| Wasmtime `index.wasm` | 85,002,487 | `0x1BA50` | `0x19E9063` | `0x5A1090` | `0x3141E11` | `CA3F772E8BE208E048F55FDB1AE84F1B80E08185BC087750364118FC90BA1407` |
| guest | 151,478,409 | `0x1618E` | `0x13D7DE9` | `0x4D1630` | `0x1421EBA` | `39433E86B3803F04C03BC381E4C98B819F82652BC1102EBE830B5F0D72DE32CC` |

相对 V254，两份主 wasm 的总大小和 CODE section 都精确减少 16 bytes，FUNCTION、DATA、
name size 不变；这对应删除两个 8-byte last/loop 构造清零。guest 的 CODE 同样减少
16 bytes；exnref conversion 后总文件另减少 26 bytes，但列出的 FUNCTION/DATA/name
size 不变，不能把额外文件 framing/custom-section 差值误算成新的算法变化。

