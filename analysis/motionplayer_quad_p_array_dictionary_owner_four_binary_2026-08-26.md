# Quad.p Array/Dictionary owner 链（四参考二进制，2026-08-26）

## 1. 四端映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Quad `p` getter | `Quad_getP_guess@0x68F0D4` | `...@0x56E7F8` | `...@0x1000F0C5C` | `...@0xECDA4` |
| Array closure helper | `createTJSArrayWithItems_guess@0x702098` | `...@0x5BAA70` | `...@0x10029FF58` | `...@0x2A4A80` |

八个函数均在本轮 fresh decompile。二进制保留了脚本属性名 `p` 及完整调用链，
但没有精确 C++ helper 符号，因此 IDB 使用带 `_guess` 的语义名；四库均写入函数
注释并保存。

## 2. Array helper 共同控制流

```text
dispatch = TJSCreateArrayObject()
if dispatch != null:
    dispatch.AddRef() // Object
    dispatch.AddRef() // ObjThis
value = Variant(Object=dispatch, ObjThis=dispatch, tag=Object)
dispatch.Release()    // balance factory owner

nativeOutput is not initialized by caller
status = dispatch.NativeInstanceSupport(
    TJS_NIS_GETINSTANCE=2, TJSGetArrayClassID(), &nativeOutput)

result.value = copy(value)
result.items = status == TJS_S_OK
    ? &nativeOutput->Items
    : null
destroy value
return result
```

四端都只接受 exact status 0；非零状态不读取 native output，直接发布 null items。
helper 不检查 `dispatch` null，后续虚调用会崩溃；没有安全 fallback。

LP64 `Items` 子对象在 native instance `+16`，ILP32 在 `+8`。这是 TJS Array
native layout 的 ABI 坐标，不进入 portable 手工 padding。

## 3. Quad.p 共同数据流

```text
result = createTJSArrayWithItems_guess()

for i = 0 .. 3:
    dictionary = TJSCreateDictionaryObject()
    accessor = ncbDictionaryAccessor(dictionary)

    accessor.SetValue(L"x", values[7 + 2*i],
                      TJS_MEMBERENSURE=512, &xHint)
    accessor.SetValue(L"y", values[8 + 2*i],
                      TJS_MEMBERENSURE=512, &yHint)

    item = Variant(Object=dictionary, ObjThis=dictionary)
    result.items.emplace_back(item)
    destroy accessor // releases its original dictionary owner

return Variant copy of result.value
destroy local result.value
```

循环严格四次。Android arm64 Hex-Rays 将 induction variable 显示成 `-1..2`，
但首轮读取 `values[7]/[8]`，随后每轮指针 `+16`，与其它三端显式 `0..3` 一致；
这是 optimizer/decompiler 表达差异。

## 4. owner 与引用计数

### 4.1 外层 Array

- factory 返回一个 raw dispatch owner；
- Variant 的 Object/ObjThis 是同一 dispatch，构造时各 AddRef 一次；
- factory owner 立即 Release；
- `Items*` 是 Array native instance 内部 deque 的 borrowed pointer，不独立 retain；
- getter 返回时 copy-construct 一个新的 Variant owner，然后销毁 local Variant。

### 4.2 每个 Dictionary

- dictionary factory 建立一个原始 owner；
- `ncbDictionaryAccessor` 正常尾部释放该 owner；
- 插入 Array 的 object Variant 对同一 dispatch 保存 Object/ObjThis 两个引用；
- Android arm64 的 deque fast path 显式出现两次 AddRef、写两个相同指针和 object
  tag，再把 cursor 增加一个 native Variant element；slow path 调 deque append helper；
- 其它三端通过 helper 实现同一 element copy/append。

因此不能用 `std::vector<Dictionary>`、单引用 wrapper 或 shared_ptr 简化。

## 5. hint 与写入边界

- `x`、`y` 使用两枚独立、进程持久的 hint word；
- 四次循环复用同一 hint pair；
- 两次 `SetValue` flags 均为 512；
- `x` 写成功、`y` 抛出时 dictionary 已有 `x`；
- Array item 只在两个 setter 正常返回后 append；
- `Items* == null` 没有 guard，第一次 append 即解引用 null。

## 6. 异常清理证据与缺口

Android arm64 landing path 明确：若当前 accessor dispatch 已建立，先切换到析构
状态并 Release；随后销毁外层 Array Variant，再 `_Unwind_Resume`。

iOS armv7 SjLj dispatcher 明确：call-site 2..4 若当前 dictionary live 则先
Release，所有可恢复 case 最后销毁外层 Array Variant 并 resume。

iOS arm64 紧邻 cold cleanup `sub_1000F0D7C@0x1000F0D7C` 明确销毁 live Variant
并 `_Unwind_Resume`；Android armv7 的 `.ARM.extab` 和 iOS arm64 LSDA 没有被
当前原生工具渲染成完整逐 call-site cleanup 表。因此正常 owner 链和两个 ABI 的
显式 landing 已证明，但 Android armv7/iOS arm64 的每个 setter/append 异常前沿
仍需 raw unwind metadata 或更强 IDA 表面补证。不能用本地 RAII 反向宣布该项完成。

## 7. 本地逐行对照

`cpp/plugins/motionplayer/RuntimeSupport.cpp` 的
`createTJSArrayWithItems_guess` 当前逐项复刻：

- Array dispatch → two-ref Variant → balance factory Release；
- output slot 不预清零；
- exact `TJS_S_OK` 才发布 `&native->Items`；
- items pointer 不拥有 native instance。

`cpp/plugins/motionplayer/PlayerLayerQuery.cpp` 的 `Quad::getP` 当前逐项复刻：

- 创建一个 Array closure；
- 四次创建 `ncbDictionaryAccessor`；
- 用 `x/y` 持久 hints 和 `TJS_MEMBERENSURE` 写值；
- 以 `tTJSVariant(dispatch, dispatch)` 进入 native Array items；
- 返回 owning Array Variant。

正常控制流和已显式恢复的 RAII 清理没有发现本地偏差，本轮不修改 C++。

## 8. 验证

当前机器没有 CMake/Emscripten，未重新运行完整 unit/Web build。现有本地测试中
有 TJS Array/Dictionary owner 基础设施覆盖，但 exact Quad.p 四 Dictionary 的异常
前沿仍应在 unwind 证据补齐后再决定是否补定向 fault-injection 测试。

## 9. 2026-08-27 EH 闭包

`motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md` 已补齐完整
上层证据：Android arm64 ordinary landing、iOS arm64 17 条 LSDA-only cold cleanup、
iOS armv7 44 条 SjLj dispatcher都会按 active state Release current Dictionary、析构 outer
Array并 resume，destructor-throw states terminate；Android armv7完整函数和相邻 catalog
无本帧 cleanup。x/y prefix、append、先前 Items、返回槽发布点和 target差异均已闭合，
`MP-L09-QUAD-P-EH` 现为 `IMPLEMENTED`。
