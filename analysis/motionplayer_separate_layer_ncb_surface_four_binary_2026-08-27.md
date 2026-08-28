# SeparateLayerAdaptor NCB constructor / `assign` surface（四参考二进制，2026-08-27）

## 1. 四端注册结构

| 端 | five-row registrar | instructions | constructor invoke | constructor allocate / attach | typed assign invoke |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6A9378` | 171 | `0x6EBECC` | attach `0x6EBFA4`; allocate `0x6EC0BC` | `0x6EC920`; unbox `0x6ECA5C` |
| Android armv7 | `0x57C5A8` | 48 | `0x5AA1C8` | combined `0x5AA258` | `0x5AAD1C`; target getter `0x5AA630`; unbox/call `0x5AADC8` |
| iOS arm64 | `0x100103080` | 42 | `0x10013D3E8` | combined `0x10013D48C` | `0x10013E198`; unbox/call `0x10013E218` |
| iOS armv7 | `0x1004A6` | 51 | `0x13DE14` | attach `0x13DE80`; allocate `0x13DFC8` | `0x13EED4`; target getter `0x13E434`; unbox/call `0x13EF58` |

四个registrar合计312条指令，均按以下固定顺序发布恰好五项：

1. 一个按值接收`tTJSVariant`的constructor descriptor；
2. `absolute`读写property；
3. `targetLayer`读写property；
4. typed direct member `clear`；
5. typed direct member `assign`。

Android arm64直接内联构造各descriptor；其余端通过相同ncbind模板helper构造。四端`assign`
descriptor都保存inner assign成员函数指针；不是raw callback，也没有脚本层宽松兼容分支。

constructor invoke / allocate / attach正常体合计529条；iOS ARMv7另外完整读取allocation cleanup
`0x13E060` 18条和attach cleanup `0x13DF68` 31条。typed assign invoke / target native
lookup / arg0 unbox-call正常体合计537条；iOS ARMv7 arg-copy cleanup `0x13F03C`另12条。相关
function均fresh decompile并完整读取disassembly；四库已命名、注释、bookmark并保存。

## 2. constructor不是自定义Factory

allocator在四端都直接执行：

```text
copy param[0] as tTJSVariant
operator new(sizeof(SeparateLayerAdaptor))
SeparateLayerAdaptor(nativeStorage, copiedVariant)
destroy copiedVariant
```

没有从descriptor读取或间接调用factory function pointer。这个形状只能对应ncbind的
`NCB_CONSTRUCTOR((tTJSVariant))`路径；若是`Factory(&fn)`，生成器必须读取已存入descriptor的
function pointer并调用它。

本地原先注册 `Factory(&SeparateLayerAdaptor::factory)`，并让该callback自行处理0参数和参数
复制。这会恢复出不存在的源结构，也会改变可观察边界。本轮删除静态factory，并将注册恢复为：

```cpp
NCB_CONSTRUCTOR((tTJSVariant));
```

类本身继续保留 `explicit SeparateLayerAdaptor(tTJSVariant)`；ncbind直接调用构造器，`explicit`
不改变生成路径。

## 3. constructor调用边界

四端共同控制流：

1. `membername != nullptr`时走method-object的非自身调用失败边界，返回`TJS_E_MEMBERNOTFOUND`
   （`-1001`）；
2. `numparams == 1 && param[0].Type() == tvtVoid`时立即返回`TJS_S_OK`；这一shell sentinel发生在
   result清空、参数计数检查、native分配和objthis/adaptor检查之前；
3. 非sentinel路径若result非null，先将其Clear为Void；
4. `numparams < 1`返回`TJS_E_BADPARAMCOUNT`（`-1004`），所以普通0参数调用不会构造默认Void
   target；
5. 只复制`param[0]`，后续参数故意忽略；
6. 分配native并以完整Variant-by-value调用构造器；
7. 通过本类ClassID从预创建的非sticky shell `objthis`取回instance adaptor；成功时只写native
   pointer；
8. objthis缺失、ClassID查询失败或adaptor为空时，运行完整SLA析构、scalar delete，并返回
   `TJS_E_NATIVECLASSCRASH`（`-1008`）。

构造器或Variant复制抛异常时，尚未提交的storage被delete；已经构造的临时Variant先析构。
attach流程抛异常时，如果native已经存在，则先运行SLA析构和delete再rethrow。iOS ARMv7两个
SJLJ cleanup逐状态显式证明这一点；destructor rollback再抛会进入terminate路径。

因此单Void sentinel创建的是`{native=null, sticky=false}`空shell；普通成功构造仍是
non-sticky owner。四端没有把现有native设为sticky、也没有替换旧native前先销毁的保护逻辑。

## 4. `assign`是严格类型化void成员

四个descriptor保存的成员函数统一还原为：

```cpp
void SeparateLayerAdaptor::assign(
    const SeparateLayerAdaptor &source);
```

包装层的共同顺序是：

1. `membername != nullptr`返回`-1001`；
2. `objthis == nullptr`返回`-1008`，且尚未清result；
3. objthis存在后，若result非null先Clear为Void；
4. `numparams < 1`返回`-1004`；surplus参数不读取；
5. 通过本类ClassID取target native；缺失、wrong-class或empty target shell返回`-1008`；
6. 复制`param[0]`为call-local Variant；strict `AsObjectNoAddRef`后按本类ClassID取source native；
7. wrong-class/non-Object source在转换器中抛异常；correct-class但native为空的shell保留ncbind
   `ToTarget<const T&>::Get(nullptr)`的尖锐空引用边界，没有被兼容成no-op；
8. 销毁call-local Variant，随后以借用const reference调用inner assign；
9. void member正常返回后包装层返回`TJS_S_OK`，result保持Void。

参数转换或inner assign抛异常时不翻译为TJS失败码；call-local Variant按EH路径析构，然后异常
继续传播。iOS ARMv7 `0x13F03C`明确只负责该临时Variant的cleanup。

本地原先的`assignCompat`会对缺参、非Object、wrong-class或empty source静默成功，并把raw
callback接收面恢复得过宽。本轮删除`assignCompat`和`assignFromAdaptor_guess`包装层，保留前一
slice已闭合的inner语义作为public typed `void assign(const SeparateLayerAdaptor&)`，注册改为
`NCB_METHOD(assign)`。这让参数计数、result、target native lookup、strict source unboxing、
surplus参数和异常生命周期全部重新由同一ncbind模板生成。

## 5. 本地验证状态

单元测试编译面新增两条断言：

- `&SeparateLayerAdaptor::assign`必须精确为`void (SeparateLayerAdaptor::*)(const
  SeparateLayerAdaptor&)`；
- 类必须能从按值`tTJSVariant`直接构造。

assign行为测试已改为直接调用typed member；它继续验证前一报告关闭的两节点sequence、完整
source Variant、source/target Object owner、属性复制和normal-only retired clear。NCB包装层本身
由仓库内与参考相同的`ncbind.hpp`模板生成，源注册形态现与四端descriptor/调用链一致。

另新增真实`Motion.SeparateLayerAdaptor`类对象测试，覆盖：普通0参数constructor返回`-1004`、
单Void产生empty shell、普通构造只消费arg0并忽略surplus、target Variant身份保持；以及真实
`assign` descriptor的nested-member `-1001`、null receiver清空前`-1008`、清空后的arity
`-1004`、wrong target `-1008`、wrong-class source抛异常、surplus忽略和void-result success。

`git diff --check`通过；coverage保持12列。正式CMake/unit/Web build仍因本机缺少CMake、
Ninja、Emscripten且没有既有build/out目录而未运行。后续
`motionplayer_separate_layer_target_layer_property_four_binary_2026-08-27.md`已关闭
`targetLayer` Variant替换；SLA类级剩余范围是所有script侧empty shell、重复attach与任意target
替换caller的最终调用点审计。constructor与typed assign包装层本报告已关闭。
