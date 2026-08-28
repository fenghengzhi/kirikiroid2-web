# EmotePlayer 模块回调与 PSB 解密根四端恢复（MP-F01/ROOT-02，2026-08-27）

## 1. 结论

`emoteplayer.dll` 在四个参考二进制中都只有同一种根拓扑：静态模块记录保存一个
pre-registration callback；callback 先加载 `motionplayer.dll`，把
`Motion.EmotePlayer` 发布到已经存在的 `Motion` namespace，再向
`Motion.ResourceManager` 注入 `setEmotePSBDecryptSeed` 和
`setEmotePSBDecryptFunc` 两个静态原生方法。

两个 setter 不拥有每个 `ResourceManager` 各自的过滤器。它们都构造一个新的
`PSB::PSBFile::OwnerFilter`，再替换同一个进程级 `std::function`。该全局对象在
motionplayer 静态根中初始化并登记析构，最后安装的 target 跨
`ResourceManager` 实例存活，直到下一次替换或进程静态析构。

本轮对模块记录、pre-registration callback、两个 setter、两个 filter invoker、
全局 target replacement 及 iOS libc++/Android libstdc++ 控制块 helper 都重新执行
fresh decompile/disassembly。四端语义相同，本地实现已经覆盖共同结构和可观察边界；
因此 `MP-F01-ROOT-02` 可从 `EVIDENCED_4_4` 升为 `IMPLEMENTED`。

## 2. 四端函数映射

### 2.1 模块记录与 pre-registration callback

| 目标 | 静态模块记录 | 完整指令数 | callback | 完整指令数 |
|---|---|---:|---|---:|
| Android arm64 | `emoteplayer_static_module_record_guess@0x42EEE0` | 31 | `emoteplayer_preRegist_callback_guess@0x67F908` | 152 |
| Android armv7 | `emoteplayer_static_module_record_guess@0x3013BC` | 33 | `emoteplayer_preRegist_callback_guess@0x5623EC` | 135 |
| iOS arm64 | `emoteplayer_static_module_record_guess@0x1001CAE20` | 26 | `emoteplayer_preRegist_callback_guess@0x1001B65DC` | 117 |
| iOS armv7 | `emoteplayer_static_module_record_guess@0x1C8EB2` | 41 | `emoteplayer_preRegist_callback_guess@0x1B645C` | 197 |

静态记录在四端都直接保存 callback 指针。Android armv7 和 iOS armv7 对 Thumb
函数指针的普通 code-xref 恢复不完整，但 registrar slot 的直接值、完整控制流和另
三端相同拓扑共同消除了歧义；这里不能把 IDA 缺失的 xref 当成源码缺失。

### 2.2 setter、filter invoker 与全局 replacement

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| seed setter | `ResourceManager_setEmotePSBDecryptSeed_and_Func_combined_guess@0x683110`；261 条，包含后述重叠入口 | `ResourceManager_setEmotePSBDecryptSeed_guess@0x564EC0`；44 条 | `ResourceManager_setEmotePSBDecryptSeed_guess@0x1001B8D68`；43 条 | `ResourceManager_setEmotePSBDecryptSeed_guess@0x1B83AC`；85 条 |
| callable setter | 同一 IDA 函数内部入口 `0x683240`；从入口读取 186 条，不能与 261 相加 | `ResourceManager_setEmotePSBDecryptFunc_guess@0x564F58`；63 条 | `ResourceManager_setEmotePSBDecryptFunc_guess@0x1001B8E50`；64 条 | `ResourceManager_setEmotePSBDecryptFunc_guess@0x1B84D0`；116 条 |
| seed filter invoke | `ResourceManager_seedFilter_invoke_guess@0x6837AC`；33 条 | `ResourceManager_seedFilter_invoke_guess@0x56522E`；33 条 | libc++ callable operator `0x1001B92E8`；31 条 | libc++ callable operator `0x1B8992`；38 条 |
| seed manager/helper | `0x683830`；28 条 | `0x565286`；25 条 | libc++ vtable/control-block helper 组 | libc++ vtable/control-block helper 组 |
| callable filter invoke | `ResourceManager_callableFilter_invoke_guess@0x6838A0`；140 条 | `ResourceManager_callableFilter_invoke_guess@0x5652C0`；90 条 | libc++ callable operator `0x1001B94A8`；61 条 | libc++ callable operator `0x1B8AB0`；104 条 |
| replace process-global filter | `ResourceManager_replaceOwnerFilter_guess@0x6A5BB0`；62 条 | `ResourceManager_replaceOwnerFilter_guess@0x57B174`；44 条 | wrapper `0x1001010B0` 4 条，body `0x1001010C4` 37 条 | wrapper `0xFE1E0` 4 条，body `0xFE1F0` 39 条 |

Android arm64 的两个 setter 被 IDA 合并为一个带内部入口的函数块；内部入口
`0x683240` 的指令流与基址函数重叠。报告分别记录两个语义入口，但不把 261 与 186
当作两段独立代码求和。

## 3. pre-registration callback 的共同数据流

四端共同伪代码如下：

```text
LoadModule(L"motionplayer.dll")
global = TVPGetScriptDispatch()
motionValue = global.PropGet(flags=0, L"Motion", receiver=global)
motion = motionValue.AsObjectNoAddRef()

setup complete native subclass descriptor for EmotePlayer
publish Motion.EmotePlayer as a static class member

managerValue = motion.PropGet(flags=0, L"ResourceManager", receiver=motion)
seedMethod = createNativeMethod(setEmotePSBDecryptSeed)
manager.PropSet(flags=0x10200, L"setEmotePSBDecryptSeed", seedMethod,
                receiver=manager)

reuse the method Variant for createNativeMethod(setEmotePSBDecryptFunc)
manager.PropSet(flags=0x10200, L"setEmotePSBDecryptFunc", method,
                receiver=manager)

destroy method Variant, then manager/motion value Variant in native cleanup order
```

`0x10200` 是 `TJS_MEMBERENSURE | TJS_STATICMEMBER`。四端 callback 都没有对取得
的 global dispatch 发出显式 `Release`；局部清理只覆盖 method/value Variant。此处
只恢复可观察调用序列，不根据未知的外部 accessor 契约额外制造释放。

四库对四个宽字符串都做过 UTF-16LE raw-byte 搜索和完整双零终止验证。Hex-Rays
偶尔只显示首 code unit 是未完全类型化的宽字符串参数造成的显示问题，不是源代码
真的发布单字符属性。

## 4. 两个 setter 的共同边界

共同伪代码：

```text
setEmotePSBDecryptSeed(result, argc, argv, objthis):
    if argc < 1: return TJS_E_BADPARAMCOUNT   // -1004
    seed64 = ordinary TJS Integer conversion of argv[0]
    next = OwnerFilter(capture seed64)
    replaceProcessGlobalOwnerFilter(next)
    return TJS_S_OK

setEmotePSBDecryptFunc(result, argc, argv, objthis):
    if argc < 1: return TJS_E_BADPARAMCOUNT
    closure = strict ObjectClosure conversion of argv[0]
    next = OwnerFilter(capture ref-counted closure)
    replaceProcessGlobalOwnerFilter(next)
    return TJS_S_OK
```

共同边界是：

- 只要求 `argc >= 1`，额外参数被忽略；
- seed 使用普通 TJS 整数转换，先捕获完整 64 位值，实际 PRNG 只取低 32 位；
- callable 使用严格 ObjectClosure 转换，不可调用值在安装前抛出；
- closure 同时保留 `Object` 和 `ObjThis`，控制块最后释放时按各自引用计数释放两者；
- replacement 先复制构造临时 `std::function`，成功后与进程级对象交换并析构旧
  target；复制失败时旧 target 保持不变；
- 四端路径均没有锁。安装和 PSB load 并发访问该进程级对象属于原生数据竞争边界，
  portable 实现不能用隐式 mutex/snapshot 改写时序。

## 5. seed filter 的精确字节流

四端共同作用区间为有符号正长度
`[header.encryptData, header.chunkOffsets)`。若二者相减后按 32 位有符号值解释为
`<= 0`，立即返回。非空时算法为：

```text
x = 123456789
y = 362436069
z = 521288629
w = low32(capturedSeed64)
remainingWord = 0

for each byte in [encryptData, chunkOffsets):
    if remainingWord == 0:
        t = x XOR (x << 11)
        x = y; y = z; z = w
        w = w XOR (w >> 19) XOR t XOR (t >> 8)
        remainingWord = w
    byte ^= low8(remainingWord)
    remainingWord >>= 8
```

这里的 refill 条件是“剩余 word 数值等于零”，不是固定每四字节 refill。因此若
生成 word 的尚未使用高字节全为零，序列会提前生成下一个 word；这一边界已经由
本地 `bytes == 0` 写法保留，不能替换成固定四字节计数器。

## 6. callable filter 的对象生命周期

四端 invoker 共同执行：

```text
accessor = new CBinaryAccessor(owner.data, uint32(owner.size))
accessorVariant = Variant(accessor)       // Variant AddRef
sizeVariant = Integer(int64(owner.size))
params = [accessorVariant, sizeVariant]
receiver = closure.ObjThis != null ? closure.ObjThis : closure.Object
closure.Object.FuncCall(flags=0, member=null, result=null,
                        argc=2, argv=params, receiver=receiver)
destroy sizeVariant/accessorVariant temporaries
ignore script result
```

四端都没有在 Variant 接管后释放 `CBinaryAccessor` 构造所得的初始引用，因此
Variant 析构只能抵消自身的 AddRef，初始引用仍存活。这是参考实现的可观察泄漏
边界，本地实现有意保留，不能按常规 RAII 直觉补一个 `Release()`。

Android armv7 的 closure 控制块清理 helper 为 `0x5653D4`、`0x5653F0`、
`0x565406`；iOS arm64 为 `0x1001B95C8`、`0x1001B95F0`、`0x1001B9620`；
iOS armv7 为 `0x1B8BFC`、`0x1B8C18`、`0x1B8C30`。它们共同证明 final release
会释放 retained `Object`，随后释放 retained `ObjThis`。Android arm64 相同清理在
合并函数/内部入口的指令流内联展开。

## 7. ABI/STL 差异与进程级 owner

- Android 两端使用旧 libstdc++ 的 `std::function` manager 形状；iOS 两端使用
  libc++ callable vtable/control block。iOS arm64 seed/callable 表分别位于
  `off_101AE9318`/`off_101AE9368`，iOS armv7 分别位于
  `off_18365E8`/`off_183660C`；表布局差异不改变两个 callable operator 的语义。
- Android arm64 编译器合并 setter 尾部，Android armv7 和两个 iOS slice 保留独立
  函数入口；这是代码生成差异。
- 32 位与 64 位捕获/控制块宽度不同，但 seed 仍是 64 位 TJS Integer，PRNG 起始
  `w` 仍只取低 32 位。
- 进程级 filter 的静态存储由 motionplayer 静态注册根初始化并登记析构：Android
  arm64 对应 `xmmword_1AB52E0` 一带，Android armv7 对应
  `dword_11117E8` 一带，iOS arm64 对应 `unk_101B697A8`，iOS armv7 在 IDA
  当前命名下落入 `motion_cubicBezierBasisCache[108]` 一带。最后一个名称只是旧
  IDA 数组误并，不可反推它属于 Bezier 源对象。
- `ResourceManager::load` 直接把同一进程级 lvalue 传给 PSB storage loader，没有
  每实例复制、锁或空值替代层。

## 8. 本地逐行对照

| 参考语义 | 本地实现 |
|---|---|
| 模块名、LoadModule、取得 Motion、发布 EmotePlayer | `cpp/plugins/motionplayer/main.cpp:675-696` |
| 取得 ResourceManager、按 `0x10200` 顺序发布两个 setter、复用 method Variant | `cpp/plugins/motionplayer/main.cpp:698-718` |
| callback 局部析构/无额外 global release 边界 | `cpp/plugins/motionplayer/main.cpp:720-723` |
| 进程级 filter 与 copy/swap replacement 语义 | `cpp/plugins/motionplayer/ResourceManager.cpp:123-132` |
| seed xorshift、低 32 位 seed、有符号非正区间与零 word 提前 refill | `cpp/plugins/motionplayer/ResourceManager.cpp:134-170` |
| Object/ObjThis closure owner 与两参数调用 | `cpp/plugins/motionplayer/ResourceManager.cpp:172-210` |
| setter 的最小 arity、普通 Integer/严格 ObjectClosure 转换与额外参数忽略 | `cpp/plugins/motionplayer/ResourceManager.cpp:281-307` |
| load 直接消费进程级 filter | `cpp/plugins/motionplayer/ResourceManager.cpp:330` |

对应单元用例位于 `tests/unit-tests/plugins/motionplayer-dll.cpp:6556-6627`，覆盖：

- 两个 setter 的零参数错误；
- seed 的 Real/String 普通整数转换与额外参数忽略；
- callable 的严格对象转换；
- callable target 跨输入 Variant 生命周期存活；
- load 时收到有效 accessor、原始 receiver 和精确 size；
- 再安装 seed target 时旧 closure 立即析构。

本轮没有新的 C++ 语义修改：上述实现和用例已与 fresh 四端证据一致。当前环境缺少
CMake/Emscripten/Ninja 及项目完整依赖，不能把源码对照和静态检查冒充正式构建或
测试；验证仅包括四端完整反编译/反汇编、UTF-16LE raw-byte 检查、调用/控制块审计、
本地逐行比较、TSV 结构校验和 `git diff --check`。

## 9. 收口与后续

`MP-F01-ROOT-02` 的模块记录、callback、两个 setter、两个 invoker、全局 owner 替换、
静态生命周期和边界行为已经闭合。下一根是 `MP-F01-ROOT-03`：从
`emoteplayer.dll` 依赖加载入口继续证明 D3D native-instance class-id 的生产者、
消费者、虚表和 owner 闭包；ROOT-02 不把整个 DrawDevice/UI 子系统自动纳入范围。
