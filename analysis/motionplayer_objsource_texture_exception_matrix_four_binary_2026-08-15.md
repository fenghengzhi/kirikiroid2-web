# ObjSource lazy texture 异常/临时量矩阵四参考复核（2026-08-15）

## 1. 范围与结论

本轮只以 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7
四份当前参考产物为依据，fresh 复核 `ObjSource_ensureTexture_guess` 从 raw PSB 像素到
`iTVPTexture2D` 发布之间的异常清理。此前三槽对象布局、texture retain、正常析构、
`drawLayer` 的 AssignTexture/SetSize 边界仍由
`motionplayer_objsource_texture_owner_lifecycle_four_binary_2026-08-14.md` 负责。

四端共同恢复出的源级结构不是事务或 RAII pipeline：decoded、BGRA 和构造完成后的 bitmap
都是 raw pointer。`CreateTexture2D` 返回值直接写入 ObjSource texture 槽，随后才 Release bitmap
并释放 BGRA。由此得到两条最重要的边界：

1. render-manager lookup 或 `CreateTexture2D` 抛出时，成员写入尚未发生，texture 仍为 null；
   已构造 bitmap 与 BGRA 没有异常 owner，故不会被本函数清理；
2. `CreateTexture2D` 正常返回后，texture 已经发布。后续 bitmap Release/BGRA dealloc 即使不能
   正常完成，也不回滚该成员。

编译器清理存在目标差异：Android arm64 与 iOS armv7 明确生成三类局部 unwind cleanup；
Android armv7 与 iOS arm64 的整个函数没有局部 landing pad/resume body。前两端也只清理
当前 PSB 临时 owner、palette vector storage 和尚未完成构造的 bitmap allocation；从不清理
raw aligned buffer 或构造完成的 bitmap。

## 2. 四端函数和生成形态

| 目标 | 函数 | 大小 | 指令数 | 函数内/相邻 unwind 形态 |
|---|---:|---:|---:|---|
| Android arm64 | `0x6D7834` | `0x60C` | 378 | 函数尾部三个 cleanup 入口，最终 `_Unwind_Resume` |
| Android armv7 | `0x599A34` | `0x2BC` | 277 | 无局部 landing pad、personality register 或 resume |
| iOS arm64 | `0x10012686C` | `0x3A8` | 224 | 无局部 landing pad 或 resume |
| iOS armv7 | `0x125D4C` | `0x3CA` | 360 | SjLj register；相邻 `0x126116`、大小 `0x8C` 的 11-case cleanup dispatcher |

地址只用于复核 reference evidence，不写进编译源码注释。剥离前源码名未知的 iOS armv7
dispatcher 在 recovery IDB 中命名为 `ObjSource_ensureTexture_unwindCleanup_guess`。

## 3. 正常流的精确提交顺序

四端尾部共同等价于：

```text
bitmapAllocation = operator new(sizeof(tTVPBitmap))
bitmap = tTVPBitmap::tTVPBitmap(bitmapAllocation, width, height, 32)
copy BGRA rows into bitmap
manager = TVPGetRenderManager()
texture = manager->CreateTexture2D(bitmap)
self.texture = texture
bitmap->Release()
AlignedDealloc(bgra)
```

对应机器位置为：

| 目标 | bitmap allocation/ctor | CreateTexture2D 后发布 | bitmap 正常释放 | BGRA 正常释放 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D7C00` / `0x6D7C14` | `0x6D7C84` | `0x6D7C90..0x6D7CA0` | `0x6D7CB8` |
| Android armv7 | `0x599C4E` / `0x599C5A` | `0x599CA2` | `0x599CA8` | `0x599CB0` |
| iOS arm64 | `0x100126B34` / `0x100126B48` | `0x100126BB8` | `0x100126BC0` | `0x100126BCC` |
| iOS armv7 | `0x126052` / `0x126064` | `0x1260C2` | `0x1260C6` | `0x1260D0` |

发布 store 全部严格位于虚 `CreateTexture2D` 调用返回之后。因此抛出不会留下半写 texture；
正常返回 null 则会写入 null、照常释放 bitmap/BGRA，随后 `drawLayer` 仍无 null guard 并自然
解引用 null。正常返回非 null 后，bitmap/BGRA cleanup 的异常不会把成员清回 null。

## 4. 显式 unwind cleanup 的精确内容

### 4.1 Android arm64

函数尾部 `0x6D7DD8..0x6D7E3C` 只有三类动作：

- `0x6D7DF8..0x6D7E20`：降低当前栈上 `PSBRawNode` owner 的 refcount，必要时销毁 owner；
- `0x6D7E24..0x6D7E34`：delete palette `std::vector<tjs_uint32>` 的 heap storage；
- `0x6D7DE8..0x6D7E34`：bitmap constructor 尚未成功返回时，delete 保存的 pending allocation；
- `0x6D7E38..0x6D7E3C`：恢复原异常。

这里没有调用 aligned deallocator、bitmap `Release` 或 ObjSource texture `Release`。pending
allocation cleanup 只覆盖 constructor 尚未完成的 new-expression 窗口；constructor 返回后，
同一 bitmap raw pointer 不再有 landing cleanup。

### 4.2 iOS armv7

主函数在每个需要局部清理的调用前写 SjLj `call_site`。相邻 dispatcher 的 11 个 case 为：

- cases 0..5：分别释放 width、height、compress、两条 pixel 路径和 pal 的当前
  `PSBRawNode` 临时 owner；
- cases 6..9：析构 palette vector 的 heap storage；
- case 10：bitmap constructor 失败时 delete pending bitmap allocation；
- 所有 cleanup 最终把 call-site 复位并调用 `_Unwind_SjLj_Resume`。

cases 6..9 只做 libc++ vector storage 的末端修正/delete；它们不读取 decoded/BGRA raw
pointer。case 10 也只 delete 尚未完成构造的 allocation，不调用完整 bitmap dtor/Release。

### 4.3 Android armv7 / iOS arm64

两份完整函数反汇编没有本地 landing pad，也没有调用 `_Unwind_Resume`、SjLj register/resume
或独立 cleanup dispatcher。这个结论只描述当前产物：优化器可以根据该平台所见的
`nounwind`/不可抛调用消掉源码级 cleanup。不能据此为 portable 源码增加手写 guard；那会在
另外两份明确保留 raw-pointer 边界的产物上制造不存在的回滚。

## 5. 阶段化异常/失败矩阵

| 阶段 | texture 成员 | 显式局部清理 | 保留边界 |
|---|---|---|---|
| strict raw-node getter 构造临时 owner 前抛出 | 仍 null | 无已构造临时量 | 原 ObjSource raw owner 不变 |
| 临时 raw-node 后续转换/GetResource 抛出 | 仍 null | A64/iOS32 释放当前临时 owner；A32/iOS64 无局部 cleanup | 先前已完成的临时 owner 已按正常流释放 |
| decoded aligned allocation 后的 decode/reverse/palette read 抛出 | 仍 null | 不释放 decoded | decoded 泄漏；对象可在下次调用重新开始 |
| palette vector 活跃期间 resize/reverse/expand 抛出 | 仍 null | A64/iOS32 释放 vector storage；A32/iOS64 无局部 cleanup | decoded/BGRA raw storage均不由 vector cleanup 接管 |
| BGRA 已分配，bitmap `operator new` 抛出 | 仍 null | 无 pending bitmap allocation | BGRA 泄漏 |
| bitmap allocation 成功、constructor 抛出 | 仍 null | A64/iOS32 delete pending allocation；A32/iOS64 无本地 landing | BGRA 仍泄漏；不调用完整 bitmap Release |
| bitmap constructor 返回后，pitch/scanline/manager lookup 抛出 | 仍 null | 不 Release bitmap，不释放 BGRA | 完整 bitmap 与 BGRA 均保留 |
| `CreateTexture2D` 抛出 | 仍 null，因为返回后 store 未执行 | 不 Release bitmap，不释放 BGRA | 下次调用会重新 materialize；旧 raw 临时量泄漏 |
| `CreateTexture2D` 正常返回 null | 写回 null | 正常 Release bitmap + 释放 BGRA | `drawLayer` 随后无 guard 解引用 null |
| `CreateTexture2D` 正常返回 texture | 已发布 retained pointer | 随后正常 Release bitmap + 释放 BGRA | ObjSource 析构最终 Release texture |
| 发布后 bitmap Release 不能正常完成 | 保持已发布 | BGRA 尚未释放 | 没有 texture rollback |
| bitmap 已释放、BGRA dealloc 不能正常完成 | 保持已发布 | bitmap 已提交释放 | 没有 texture rollback |

上述“抛出”只用于描述调用真的向外传播异常时的清理图；若某平台调用被编译器证明
`nounwind`，对应阶段不会进入 C++ unwinder。矩阵不把内存分配失败的 ABI 终止路径伪装为
可捕获脚本异常。

## 6. Portable 源码含义

`cpp/plugins/motionplayer/SourceCache.cpp` 已经使用参考源级形态：两个 aligned raw pointer、
palette `std::vector`、raw bitmap pointer、CreateTexture2D 返回后直接 member assignment、再按
bitmap/BGRA 顺序正常清理。本轮不引入 `unique_ptr`、scope guard、try/catch 或 rollback；这些
“安全化”都会改变四端共同可观察边界。

本轮只补充语义注释，明确：

- decoded/BGRA 没有异常 owner；
- pending bitmap allocation cleanup 与完整 bitmap ownership 是两个不同阶段；
- CreateTexture2D 抛出发生在 member store 之前；
- store 之后的正常 cleanup 不回滚 texture。

## 7. IDB 与验证

四份 recovery IDB 的 ensureTexture 入口、CreateTexture2D publish store 与异常边界均补充了
注释；Android arm64 cleanup tail、iOS armv7 dispatcher/cases 另加精确注释和 bookmark，
iOS armv7 dispatcher 使用 `_guess` 语义名。四库随后原位保存。

源码没有改变执行语句。仍执行 motionplayer unit-test 翻译单元 syntax check、Web Debug
完整构建与 `git diff --check`，以确认注释/文档迁移没有破坏工作区编译。

