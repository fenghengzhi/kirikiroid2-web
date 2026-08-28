# SeparateLayerAdaptor public `clear` 与析构（四参考二进制，2026-08-27）

## 1. 完整证据

| 端 | public clear | instructions | destructor bridge | instructions | cleanup |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6A965C` | 101（含landing pads） | `0x6CD398` | 34 | bridge内A64 landing path |
| Android armv7 | `0x57C698` | 124 | `0x593E98` | 15 | — |
| iOS arm64 | `0x1001031AC` | 145 | `0x10012A644` | 19 | unwind metadata |
| iOS armv7 | `0x100590` | 208 | `0x1291F8` | 60 | `0x12929A`, 23 |

四端clear合计578条，四端destructor bridge合计128条，iOS ARMv7显式SJLJ cleanup另23条；
总计729条，全部fresh decompile并完整读取disassembly。A64 IDA oversized range在clear landing
pads之后还包含独立assign secondary entry；clear统计只到assign prologue之前，避免重复计算。

四端clear/dtor已命名、注释、bookmark并保存。iOS ARMv7 cleanup明确调用
`clang_call_terminate`，A64 landing path执行相同成员清理后进入terminate helper。

## 2. public clear只处理private与active

共同顺序：

1. 检查持久private target Variant的真实类型；
2. 类型为Object时，直接对其Object调用 `Invalidate(0,null,null,self)`；
3. 无论Object Invalidate HRESULT是什么，正常返回后立即Clear持久private Variant；非Object也
   直接Clear；
4. 按key升序复制并Invalidate active树的每个完整payload；
5. 全部active临时payload成功后才销毁原active树并重置空header/count；
6. 返回，不读取也不修改retired树。

private target没有call-local Variant copy或额外Object-only owner；持久Variant本身保证调用期间
owner存活。Invalidate异常发生时第3步尚未执行，private Variant仍保留。普通失败HRESULT被
忽略，private仍立即清空。

active阶段复用上一报告关闭的完整payload-copy语义：Layer Variant保持到临时payload最后
析构；某节点Invalidate异常时原active树完整保留，但较小key的Invalidate副作用会在重试时
重复。由于private已在进入active循环前清空，active异常不会把private恢复。

retired故意完全不碰。这允许异常pass的retired状态在下一次begin whole-tree swap时重新进入
active。给public clear增加“顺手清retired”的合理化会破坏这条恢复路径。

## 3. 析构正常路径

destructor bridge先调用public clear。正常返回后按成员反向顺序：

1. 析构retired map，不调用Layer Invalidate，只释放payload/Layer owners；
2. 析构active map；正常clear已把它清空；
3. 析构private Variant；正常clear已将它设Void；
4. 析构target Variant；
5. 析构owner Variant。

absolute与sequence为trivial字段。bridge只完成native object析构并返回this；operator delete由
外层ncbind adaptor/factory rollback负责，不属于这个bridge。

本地声明顺序是owner、target、private、active、retired、absolute、sequence，且
`~SeparateLayerAdaptor()`先调用`clear()`；两个map析构使用 `clear(false)`。因此正常路径与
四端一致：active/private获得显式Invalidate，retired只做release。

## 4. destructor异常与terminate边界

析构器是noexcept边界。public clear中的private/active Invalidate或payload copy若抛异常：

- 编译器cleanup仍析构retired、active、private、target、owner；
- 这些成员析构不补发Invalidate，只释放当前owners；
- iOS ARMv7 `0x12929A`在成员清理后调用 `clang_call_terminate`；非法SJLJ state直接abort；
- A64 landing path同样先清成员，再进入terminate helper；异常不会返回caller。

这与显式调用public `clear()`不同：普通方法调用的异常可以传播给caller并保留未清树，供重试
或下一pass使用。不能为析构单独catch并吞异常，也不能让public clear变成noexcept。

## 5. 测试与本地状态

本轮没有改动已经匹配的public clear/dtor源控制流；它依赖上一slice刚修正的payload-copy
Layer Variant析构时机。现有 `SeparateLayer assign reuses shared publication hints`测试新增：

1. target active有三个ordinal；
2. begin-pass把它们整体交换到retired；
3. public clear不产生任何Invalidate；
4. 第二次begin把这些节点交换回active；
5. public clear对三Layer各Invalidate一次。

测试以独立Variant持有三probe，避免map clear释放最后owner后再读取悬空raw pointer。

`git diff --check`通过；coverage保持12列。正式CMake/unit/Web build仍因本机缺少CMake、
Ninja、Emscripten且没有既有build/out目录而未运行。后续
`motionplayer_separate_layer_ncb_surface_four_binary_2026-08-27.md`已关闭constructor attach失败
rollback与public typed assign wrapper的参数/native-instance/result ABI；后续
`motionplayer_separate_layer_target_layer_property_four_binary_2026-08-27.md`也已关闭
`targetLayer`纯Variant替换。类级剩余范围为empty shell与任意target替换后的caller链审计。
