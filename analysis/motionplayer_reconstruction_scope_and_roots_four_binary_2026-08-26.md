# Motionplayer 恢复范围、根入口与四端基线（MP-F01/MP-F02，2026-08-26）

## 1. 目的

本报告建立 motionplayer 恢复工作的覆盖分母起点。它只回答两个问题：

1. 哪些原生模块记录和运行时入口构成 motionplayer 的根；
2. 本轮使用的四个参考目标及配套 IDB 是否一一对应且可反编译。

本报告不从本地 `cpp/plugins/motionplayer/` 推导参考实现。根入口先由四个参考
二进制中的 UTF-16LE 模块名、交叉引用、静态注册记录和 fresh decompile 证明；
本地代码只在证据成立后作为实现映射。

## 2. 范围规则

### 2.1 核心根

- `motionplayer.dll` 的 NCB 静态注册链；
- `emoteplayer.dll` 的 NCB 模块记录与模块回调；
- `emoteplayer.dll` 回调加载 `motionplayer.dll` 后取得的 `Motion`、
  `EmotePlayer`、`ResourceManager` 和两个 PSB decrypt callback 发布链；
- 加载 `emoteplayer.dll` 后取得 `D3DLayerObjectNativeInstance` 的 D3D 依赖链。

### 2.2 依赖闭包

从上述根出发，在每个参考二进制内沿直接调用、虚表、NCB 注册函数指针、静态
对象回调和被引用数据向外扩展。只有具备该可达性证据的共享 TJS、Layer、
DrawDevice、Texture、Storage、PSB 或平台适配代码才进入 motionplayer 恢复范围。

### 2.3 排除规则

通用 movie/FFmpeg、UI、网络或其它插件代码不能因为位于同一大二进制、文件名中
出现 `motionplayer`，或本地代码存在相似类型而自动纳入。只有四端根可达性成立
时才进入依赖闭包。一次空字符串搜索、空 `rg` 或缺少当前 IDA 名称也不能证明
某实体不存在；这类条目只能标为 `UNMAPPED` 或 `EVIDENCE_BLOCKED`。

## 3. 四个目标与 IDB 身份

本轮先枚举会话，确认无旧会话后分别以原生 `mcp__idalib__idb_open` 打开四个
配套 `.i64`。随后对每个会话执行 `server_health` 和 `survey_binary(minimal)`。

| 目标二进制 | SHA-256 | database | IDB module / input | 位数与格式 | imagebase | Hex-Rays | 函数数 |
|---|---|---|---|---|---:|---|---:|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `05e2ff4c77f1561608ad7703153d2fb09855bf223237a85dc2267fff1388564f` | `mp_android_arm64` | module 与 input 均为该 `.so` | ELF AArch64, 64-bit | `0x0` | ready | 55,934 |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `a15c238ec6f21c17d0889b064ae1ad47ec85b4f1530a3611f206b7190ff456af` | `mp_android_armv7` | module 为该 `.so`；IDB 保留创建机 Windows input path | ELF ARM EABI5, 32-bit | `0x0` | ready | 67,371 |
| `Kirikiroid2_1.3.9_iOS_arm64` | `733ba5d3fd0798e41ddbac0f0a5b484e7cd20443ee5313781e0e32d1633e18e3` | `mp_ios_arm64` | module 为 `Kirikiroid2_1.3.9_iOS_arm64` | fat Mach-O 的 arm64 slice, 64-bit | `0x100000000` | ready | 68,365 |
| `Kirikiroid2_1.3.9_iOS_armv7` | `733ba5d3fd0798e41ddbac0f0a5b484e7cd20443ee5313781e0e32d1633e18e3` | `mp_ios_armv7` | module 为恢复出的 `Kirikiroid2_1.3.9_iOS_armv7.thin-armv7` | 同一 fat Mach-O 的 armv7 slice, 32-bit | `0x4000` | ready | 64,664 |

两个 iOS 路径当前内容哈希相同，因为它们都是含 armv7 与 arm64 的同一 fat
Mach-O；两个配套 IDB 分别分析不同 slice，不能把地址跨库复用。

`auto_analysis_ready=false` 是本轮以 `run_auto_analysis=false` 接管已经恢复的 IDB
所得状态；四库的 `status=ok`、字符串 cache 和 Hex-Rays 均 ready，且本轮所有
目标函数 fresh decompile 成功，因此不构成取证缺口。

## 4. 字符串搜索与 IDB 类型修正

对 `motionplayer.dll` 和 `emoteplayer.dll`，四库的
`find(type=string)` 都返回 0。按 UTF-8、UTF-16LE、UTF-32LE 补做
`find_bytes` 后，四库都只在 UTF-16LE 找到匹配。读取匹配前后原始字节确认了
完整字符串和双零终止符。

同样方法确认了 `setEmotePSBDecryptSeed` 和
`setEmotePSBDecryptFunc`。此前 Hex-Rays 中的 `"m"`、`"e"`、`"s"`
只是把 UTF-16LE 首 code unit 当 ASCII 的错误展示，不是原始源码单字符键。

本轮已在四库中把所有本轮命中的模块名及两个 decrypt callback 名设置为正确的
`unsigned short[N]` 数组类型，重新反编译根函数，并分别 `idb_save`。部分外层
未类型化 registrar slot 或 helper 原型仍会使 Hex-Rays 在赋值/调用点显示首字符；
原始字节、data item 类型和同地址 xref 已确认完整宽字符串。后续恢复相应
registrar/helper 类型时继续消除这些显示残留，不能把显示残留重新解释为源码差异。

## 5. 四端根映射

### 5.1 `motionplayer.dll` 静态注册根

| 二进制 | 函数 | 状态 | fresh 证据摘要 |
|---|---|---|---|
| Android arm64 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_42F1F8@0x42F1F8` | 已定位 | 初始化共享 geometry 容器/缓存并把 `BezierPatch`、`Motion` registrar 接入模块链；模块名原始字节为 `motionplayer.dll` |
| Android armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_3016E8@0x3016E8` | 已定位 | 明确三次写入 `L"motionplayer.dll"`，建立 `BezierPatch` attach 与 `Motion` registrar |
| iOS arm64 | `Kirikiroid2_1.3.9_iOS_arm64!InitFunc_49@0x10014FC74` | 已定位 | 明确三次写入 `L"motionplayer.dll"`，建立同一 registrar 链 |
| iOS armv7 | `Kirikiroid2_1.3.9_iOS_armv7!InitFunc_49@0x151C98` | 已定位 | 明确三次写入 `L"motionplayer.dll"`，建立同一 registrar 链 |

四端共同源码结构：

```text
initialize motion geometry/cache globals
append Layer-attached BezierPatch registrar to motionplayer.dll list
register cleanup for the registrar's callable storage
append Motion native class registrar to the same module list
```

2026-08-27 对四个根重新 fresh decompile 并完整读取 80/85/66/98 条指令后，直接
module-list 分母可以封口：四端都恰好插入两条 registrar record——Layer-attached
`BezierPatch` 与顶层 `Motion`；不存在第三条 direct motionplayer.dll record。
`Motion` registrar 随后按固定顺序展开 23 个常量、11 个 delayed subclass 和两个
namespace method；该闭包已经由 `motionplayer_ncb_equivalence.tsv` 的 316/316 四端映射
及各 subclass/body 切片逐项承接。BezierPatch 的 8 行 attached 表、Motion 的 11 个
subclass 和两个方法也已有独立报告，因而“枚举每个 module-list 可达 registrar”的旧剩余项
不再开放。

Android 使用 libstdc++，iOS 使用 libc++，全局容器初始化与析构 helper 展开不同；
32/64 位 registrar slot 宽度不同。这些是 ABI/STL 差异，不改变共同模块拓扑。

### 5.2 `emoteplayer.dll` 模块记录与回调

| 二进制 | 静态注册函数 | 模块回调 | 状态 |
|---|---|---|---|
| Android arm64 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_42EEE0@0x42EEE0` | `...!sub_67F908@0x67F908` | 两者已 fresh decompile；静态记录直接保存 callback 指针 |
| Android armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_3013BC@0x3013BC` | `...!sub_5623EC@0x5623EC` | 已定位；Thumb xref 数据识别不足不改变 registrar slot 中的直接函数指针证据 |
| iOS arm64 | `Kirikiroid2_1.3.9_iOS_arm64!InitFunc_62@0x1001CAE20` | `...!sub_1001B65DC@0x1001B65DC` | 已定位；静态记录直接保存 callback 指针 |
| iOS armv7 | `Kirikiroid2_1.3.9_iOS_armv7!InitFunc_62@0x1C8EB2` | `...!sub_1B645C@0x1B645C` | 已定位；Thumb xref 缺失由 slot 直接函数指针和四端控制流补足 |

共同模块回调伪代码：

```text
motionModule = LoadModule(L"motionplayer.dll")
motionClass = globalObject.PropGet(L"Motion")
register Motion.EmotePlayer using the recovered native class descriptor
resourceManager = motionClass.PropGet(L"ResourceManager")
seedCallback = createNativeFunction(seedHandler)
resourceManager.PropSet(flags=66048, L"setEmotePSBDecryptSeed", seedCallback)
decryptCallback = createNativeFunction(decryptHandler)
resourceManager.PropSet(flags=66048, L"setEmotePSBDecryptFunc", decryptCallback)
destroy callback variants and the retained module variant in native order
```

反编译中 Android arm64 第一段能直接显示 `setEmotePSBDecryptSeed`，其余若干调用点
仍显示首字符；四端原始 UTF-16LE 字节分别确认两个完整键，并且调用顺序一致。

2026-08-27 已继续展开该根的完整依赖：两个 setter 的最小 arity/类型转换、seed
xorshift 字节流、callable 的 `Object`/`ObjThis` owner、`CBinaryAccessor` 初始引用
泄漏边界、进程级 `OwnerFilter` replacement 和静态析构均完成四端 fresh 审计，并与
本地实现逐行对照。完整函数映射、伪代码、ABI 差异和验证见
`motionplayer_emoteplayer_module_decrypt_root_four_binary_2026-08-27.md`。
`MP-F01-ROOT-02` 因而已经闭合；后续不再以“展开 decrypt callback callee”为开放项。

### 5.3 `emoteplayer.dll` 的 D3D 依赖加载根

| 二进制 | 函数 | 状态 |
|---|---|---|
| Android arm64 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_53101C@0x53101C` | 已定位/fresh decompile |
| Android armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_49516C@0x49516C` | 已定位/fresh decompile |
| iOS arm64 | `Kirikiroid2_1.3.9_iOS_arm64!sub_1002335C8@0x1002335C8` | 已定位/fresh decompile |
| iOS armv7 | `Kirikiroid2_1.3.9_iOS_armv7!sub_2323C0@0x2323C0` | 已定位/fresh decompile |

共同伪代码：

```text
initialize cached dispatch/member descriptor once
LoadModule(L"emoteplayer.dll")
resolve native class id for L"D3DLayerObjectNativeInstance"
publish the class id into the D3D native-instance global
```

该链证明 D3D shell/native-instance 体系属于当前依赖闭包；它不证明整个 DrawDevice、
movie 或 UI 子系统都属于范围，后续仍需逐边扩展可达性。

2026-08-27 已把这条根扩展到两个不同的内部 native identity、所有 class-id producer/
consumer、两份 adaptor vtable 和 owner 生命周期。`D3DLayerBase` 是完整 ClassInfo +
sticky root view；`D3DLayerObjectNativeInstance` 是单 class-id word + 永不拥有 payload
的 borrowed view。完整四端映射、xref 分母、伪代码、四槽重复注册/失败泄漏边界和
本地逐行对照见
`motionplayer_drawdevice_d3d_dependency_root_four_binary_2026-08-27.md`。
`MP-F01-ROOT-03` 已闭合。

## 6. 本地映射

- `cpp/plugins/motionplayer/main.cpp` 同时声明 `motionplayer.dll` 与
  `emoteplayer.dll`，与四端模块拓扑一致；
- `cpp/plugins/motionplayer/CMakeLists.txt` 将当前 motionplayer translation units
  链接为一个静态目标，这只是 portable 构建边界，不能反向证明原始 translation-unit
  切分；
- `cpp/plugins/motionplayer/main.cpp` 中的 `Motion`、`EmotePlayer`、
  `ResourceManager` 和 D3D 表面从现在起必须逐成员回填到覆盖总账，不能因本地已存在
  注册宏而预标为 `EVIDENCED_4_4`。

本轮不修改任何 C++ 运行语义。

## 7. MP-F01/MP-F02 结论

- MP-F01 的 direct motionplayer root、emoteplayer callback/decrypt root 与
  DrawDeviceD3D dependency root 均完成：根入口、恰好两条 direct registrar、其
  Motion/Bezier 下游注册分母、依赖闭包和排除规则已经由四端证据固定；
- MP-F02 完成：四端目标/IDB 身份、位数、基址和 Hex-Rays 可用性已核对；
- MP-F03 开始：本报告中的三个根等价类是覆盖总账的首批记录；
- 其余所有类、函数、对象、容器和 owner 边仍必须由总账逐项证明，不能用本报告
  的根可达性替代函数级四端取证。
