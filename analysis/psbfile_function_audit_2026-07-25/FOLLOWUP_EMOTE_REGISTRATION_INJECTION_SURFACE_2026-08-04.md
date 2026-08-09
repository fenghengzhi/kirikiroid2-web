# EmotePlayer 注册入口 / setter 注入面闭环（2026-08-04）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

沿上一轮 `OwnerFilter` producer 继续向上追踪后，发现本地虽已恢复两个 setter 的函数体与
`Motion.EmotePlayer` 完整成员表，模块注册架构仍不一致：

- `Motion` 主注册表提前包含 `EmotePlayer`；
- `emoteplayer.dll` 被拆成 pre-load 与 post-injection 两个 callback；
- callback 带本地自造的空指针、返回码与 Variant 类型 guard；
- 两个 native method 分别构造独立 Variant；
- 本地显式 `Release` 了 `TVPGetScriptDispatch()` 返回值。

Android 只有一个 `emoteplayer.dll` init callback。它在一个函数内依次加载
`motionplayer.dll`、取得 `Motion`、创建并挂接完整 `EmotePlayer` class、取得
`Motion.ResourceManager`，再把两个 setter 注入进去。本轮已按该结构重写本地入口，并从
`motionplayer.dll` 主表删除提前注册。这个生产 GAP 已关闭。

## fresh 反编译证据

本轮 fresh decompile：

- `emoteplayer_static_init@0x42EB00..0x42EB84`；
- `emoteplayer_entry@0x682528..0x6827A8`；
- `EmotePlayer_loadClass@0x685BC0..0x685D30`；
- `EmotePlayer_ncb_registerMembers@0x67FAC8..0x681680`；
- `motionplayer_ncb_register@0x6D9B08..0x6DA28C`；
- `EmotePlayer_setEmotePSBDecryptSeed_callback@0x685D30..0x685E60`；
- `EmotePlayer_setEmotePSBDecryptFunc_callback@0x685E60..0x686148`；
- `TVPGetScriptDispatch_guess@0x8E3C20` 及其 AddRef tail target。

关键逻辑压缩为十行：

```text
LoadModule("motionplayer.dll")
global = TVPGetScriptDispatch(); value = global.Motion; motion = value.AsObject()
EmotePlayer_loadClass("EmotePlayer", true); attach that class to motion with flags 0x10000
value = motion.ResourceManager
seed = CreateNativeMethod(seed_cb); methodValue = Object(seed, seed); seed.Release()
manager = value.AsObject(); manager.PropSet(0x10200, seedName, methodValue, manager)
func = CreateNativeMethod(func_cb)
methodValue.SetObject(func, func); func.Release()
manager.PropSet(0x10200, funcName, methodValue, manager)
destroy methodValue, then reused value; do not explicitly Release global
```

## 唯一模块入口

`emoteplayer_static_init@0x42EB00` 只构造一只 `emoteplayer.dll` auto-register node：

- `0x42EB50/58` 唯一物化 `emoteplayer_entry@0x682528`；
- `DUP V0.2D, XZR` 先把 init/term 两槽清零；
- `INS V0.D[0], X8` 只把 entry 写入 init 槽；
- `STUR Q0` 发布 `init=entry, term=null`。

完整 `.text` ADRP/ADD 扫描没有第二处 `0x682528` 物化，因此不存在另一个同名模块入口或
独立 post callback。`emoteplayer.dll` 的 UTF-16 名位于 `0x14C2878`。

## entry 的 Variant / refcount 顺序

`emoteplayer_entry` 的顺序不能简化为“最终属性相同”：

1. `Motion` 的 PropGet 与 `ResourceManager` 的 PropGet 共用栈上同一 `tTJSVariant`；
2. `EmotePlayer_loadClass` 返回值被忽略，随后以 type `0`、flags `0x10000` 挂到 Motion；
3. seed native method 先构成 `Object/ObjThis` 相同的 Variant，并对两槽各 AddRef，随后释放
   raw method；
4. 只有这之后才把复用的 ResourceManager Variant 强制转换为 Object；
5. 第一条 PropSet 使用 `0x10200 = TJS_MEMBERENSURE | TJS_STATICMEMBER`；
6. func native method 通过 Variant `SetObject(func, func)` 覆盖同一 method Variant，旧 seed
   method 的两个引用在覆盖时释放；
7. 第二条 PropSet 使用相同 flags；函数尾只析构两个 Variant。

`TVPGetScriptDispatch_guess@0x8E3C20` 的 tail target 对全局 dispatch 执行 AddRef，但 entry
没有对应显式 Release。本地因此保留这个对象生命周期边界，不用“更安全”的平衡 Release
改写原始行为。

两个 setter callback 的函数地址分别只在 `0x68261C/20`、`0x6826BC/C0` 被物化；两个
UTF-16 setter 名也分别只有 entry 内的引用。它们不是 ResourceManager 12-member registrar
的一部分。

## Motion 主注册表的排除边界

`motionplayer_ncb_register@0x6D9B08` 的 subclass call edge 精确为 11 条：

```text
Point -> Circle -> Rect -> Quad -> LayerGetter -> Player -> SourceCache ->
ObjSource -> ResourceManager -> SeparateLayerAdaptor -> D3DAdaptor
```

其中没有 `EmotePlayer`，也没有到 `emoteplayer_entry@0x682528`、
`EmotePlayer_loadClass@0x685BC0` 或 `EmotePlayer_ncb_registerMembers@0x67FAC8` 的 direct
edge；该 registrar 内亦没有 `EmotePlayer` UTF-16 字面量物化。换言之，类的最终 owner 是
Motion，但类的创建/挂接 owner 是独立 `emoteplayer.dll` entry。

后续同日审计已继续关闭 Player 注册机制差异：本地现在也在上述第六个 in-flow subclass
row 注册 `Motion.Player`，不再创建 `global.Player` 或执行 post alias；23 个常量与两个
namespace function 也已恢复到同一 registrar。详见
[FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md](FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md)。

## 本地逐行对照

| Android 数据流 | 本地实现 | 对照 |
| --- | --- | --- |
| Motion 主表不含 EmotePlayer | `main.cpp:600-646` | 23/11/2 主表中没有 EmotePlayer row |
| 独立模块唯一 callback | `main.cpp:709-755` | `emoteplayer.dll` 只有一只 `NCB_PRE_REGIST_CALLBACK` |
| LoadModule 后取得 Motion | `main.cpp:714-721` | 无本地 guard，复用 `value` Variant |
| class init + 完整 member table | `main.cpp:724-729` | `Setup(..., true)` 后挂同一 class object，flags `0x10000` |
| 复用 Variant 取得 ResourceManager | `main.cpp:733-739` | 顺序与 entry 一致 |
| seed method Object/ObjThis + Release | `main.cpp:734-742` | 单一 `methodValue`，flags `0x10200` |
| SetObject 覆盖为 func method | `main.cpp:744-750` | 复用同一 Variant，不用第二只临时 |
| global 无显式 Release | `main.cpp:752-753` | 保留 entry 的 AddRef 边界 |
| 首次加载前/后注册边界 | `motionplayer-dll.cpp:102-180` | 首次先断言不存在，加载后断言类与两个 setter 都是 Object |

## 机械门禁

`verify_elf_surface.py` 新增输出：

```text
emote_registration_surface=true fdes=8 manifest_fdes=0 external_fdes=8 utf16=7 forbidden_motion_callback_literals=0 materializations=3 entry_materializations=1 setter_materializations=2 direct_edges=11 motion_constants=23 motion_subclasses=11 motion_function_materializations=2 motion_functions=2 forbidden_motion_hits=0 semantic_words=52 byte_ranges=5 range_bytes=3404 single_entry=true setters_in_entry=true sha256=true
```

后续 Motion registrar / module static-init 扩展后，门禁固定 10,062-byte canonical
surface，SHA-256 为
`646464a8fc8db91f853a2de15df7105b66611645344ba23ec6e8b5adf3f46a9d`，并验证：

1. 八个相关 FDE 的精确边界（含后续纳入的 motionplayer static initializer）；
2. 七个 UTF-16 模块/类/成员名；
3. 完整 `.text` 恰好一处 entry callback 与两处 setter callback 物化；
4. entry/class loader 的 11 条 direct BL edge；
5. Motion registrar 的 23 条 constant edge、11 条 subclass edge、两个 function callback/
   member-add，以及 EmotePlayer direct/name 引用为零；
6. 52 个 source-facing exact word；
7. emote static init、motion static init、entry、class loader、Motion registrar 五只完整 FDE，
   共 3,404 raw bytes；
8. 两个二进制不存在的 `ShortCutInitial*KeyMap` UTF-16 literal 保持全 ELF 零命中。

## 验证

- Web Debug 已重新配置并完整链接 `index.html`；
- macOS Debug `motionplayer-dll` 重新编译、链接成功；
- 21/21 test cases、1555/1555 assertions 通过；
- 测试没有新增或构造 fixture，只复用既有进程级 NCB runtime；
- 完整 ARM64 ELF 门禁与审计一致性门禁见总报告的最终验证记录。
