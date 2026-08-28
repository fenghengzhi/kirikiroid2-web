# SeparateLayerAdaptor `targetLayer` property（四参考二进制，2026-08-27）

## 1. 四端完整证据

| 端 | native getter | native setter | NCB PropGet | NCB PropSet |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6A9648` | `0x6A9654` | `0x6EC514`; invoke `0x6EC758` | `0x6EC600` |
| Android armv7 | `0x57C686` | `0x57C692` | `0x5AA860`; invoke `0x5AA994` | `0x5AA8EC`; invoke `0x5AAA20` |
| iOS arm64 | `0x100103198` | `0x1001031A4` | `0x10013DBB8`; invoke `0x10013DD84` | `0x10013DC58` |
| iOS armv7 | `0x10057C` | `0x100588` | `0x13E7AC`; invoke `0x13E898`; cleanup `0x13E952` | `0x13E812`; invoke `0x13E98C`; cleanup `0x13EA42` |

四端native getter/setter thunk合计24条指令。四个PropGet wrapper与invoke完整range合计383条，
四个PropSet wrapper与invoke完整range合计350条；A64 landing paths已包含在各自range内，iOS
ARMv7两个显式SJLJ cleanup另30条。总计787条，全部fresh decompile并完整读取disassembly。

前一NCB registrar报告已证明该property位于五行表的第三项，getter与setter指针均非null。相关
函数均已命名、注释、bookmark并保存到四份IDB。

## 2. native getter只返回完整Variant副本

四端getter都直接对native对象中的第二个持久Variant执行copy construction并返回：

```cpp
tTJSVariant SeparateLayerAdaptor::getTargetLayer() const {
    return _targetLayer;
}
```

没有Object-only owner、Layer native unwrap、`AsObject`、fallback到private/owner、owner重新读取或
类型检查。返回值保留Object与ObjThis两部分closure；任意Variant类型均原样返回。

NCB PropGet的共同边界：

1. `membername != nullptr`返回`TJS_E_MEMBERNOTFOUND`（`-1001`），result不变；
2. getter指针为空会返回`TJS_E_ACCESSDENYED`（`-1007`），本property不可达；
3. `objthis == nullptr`返回`TJS_E_NATIVECLASSCRASH`（`-1008`），result仍不变；
4. receiver存在后，若result非null先Clear为Void；
5. 按本类ClassID取native；wrong-class或empty shell返回`-1008`，result保持Void；
6. native getter构造第一个返回Variant；invoke层再copy-construct第二个临时Variant；
7. result非null时把第二个临时copy-assign进去；result为null仍执行getter与两个临时生命周期；
8. 按第二个临时、getter返回临时的逆序析构并返回`TJS_S_OK`。

iOS ARMv7 `0x13E952`证明getter或result赋值抛异常时，只析构已经进入live state的第二个临时，
随后总会析构getter返回Variant，再resume。

## 3. setter是纯Variant-by-value替换

四端setter thunk只执行：

```cpp
void SeparateLayerAdaptor::setTargetLayer(tTJSVariant value) {
    _targetLayer = value;
}
```

NCB PropSet共同顺序：

1. nested `membername`返回`-1001`；setter不存在返回`-1007`；
2. null receiver返回`-1008`；
3. receiver有效但`param == nullptr`返回`TJS_E_FAIL`（`-1`）；
4. 按本类ClassID取native；wrong-class或empty shell返回`-1008`；
5. 将脚本param完整copy-construct为setter的按值`tTJSVariant`参数；
6. setter用标准`tTJSVariant` copy assignment替换持久`_targetLayer`；
7. setter返回后析构按值参数，wrapper返回`TJS_S_OK`。

参数不是Layer类型，也不要求Object。Integer、String、Void、Object/ObjThis closure均可写入；
malformed值只会在后续真正把targetLayer当对象使用的caller处暴露尖锐边界。

按值参数先完整持有新closure，再进入持久字段copy assignment，因此旧/new引用别名仍经过标准
Variant owner顺序；没有手写“先Clear旧值”的空窗。但setter不会做以下任何操作：

- 不重新读取新target的`window`，所以`_owner`保持constructor时的值；
- 不Invalidate旧target；
- 不清理或重建private target；
- 不清active/retired map；
- 不reset absolute或per-pass sequence；
- 不验证新target尺寸、Layer ClassID或dispatch能力。

setter、Variant copy或old-owner Release callback若抛异常，生成层只析构已构造的按值参数并继续
传播；无catch、无状态回滚。iOS ARMv7 `0x13EA42`明确给出这一单临时cleanup。

## 4. 本地实现与测试

本地原有声明已经精确匹配四端，不需要改控制流：

```cpp
tTJSVariant getTargetLayer() const { return _targetLayer; }
void setTargetLayer(tTJSVariant v) { _targetLayer = v; }
```

真实`Motion.SeparateLayerAdaptor`类对象测试扩展为直接取得`targetLayer` property descriptor，并
验证：

- null receiver在result clear前返回`-1008`；wrong receiver在clear后返回`-1008`；
- valid receiver + null param返回`-1`；wrong receiver setter返回`-1008`；
- Integer 77可写入并由typed getter原样返回；
- Void可继续替换；
- owner Variant与absolute=23在两次替换后均保持不变。

这同时锁定“exact Variant property”与“setter无owner/scalar副作用”。active/retired/private的
联合生命周期已由ordered-map、clear/dtor与assign报告分别关闭；本property不触碰它们。

`git diff --check`通过；coverage保持12列。正式CMake/unit/Web build仍因本机缺少CMake、
Ninja、Emscripten、Boost headers且没有既有build/out目录而未运行。SLA公开property本slice已
关闭；剩余类级工作是审计所有script caller是否会把单Void constructor产生的empty shell或
被任意Variant替换后的targetLayer送入后续尖锐消费点。
