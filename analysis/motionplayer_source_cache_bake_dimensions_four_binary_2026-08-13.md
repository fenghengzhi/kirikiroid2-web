# SourceCache::bakeSource 尺寸读取与 byteWeight 四端复核（2026-08-13）

> **V184 补充（2026-08-17）**：尺寸双阶段读取结论不变；其前后的 `drawLayer` 与低 blend
> scratch calls 共享同一个非空 result Variant，而不是传 null。详见
> `analysis/motionplayer_source_cache_bake_shared_result_hint_family_boundary_four_binary_2026-08-17.md`。

## 结论

四个当前参考目标在 `source.drawLayer(entry.layer)` 之后，以 ncbind 的
`getIntValue(name, 0)` 语义依次读取 `width`、`height`：

1. 先用 `TJS_MEMBERMUSTEXIST`（`0x400`）调用一次 `PropGet` 作为存在性
   probe；
2. probe 返回负值时直接使用调用方默认值 `0`，不做第二次读取；
3. probe 成功时再以 flags `0` 做一次普通 `PropGet`，把新 Variant 转为
   32 位 `tjs_int`；第二次调用的错误码本身不检查，因此失败后保持 void
   的结果会按 ncbind 转换语义落为 `0`；
4. 随后以 32 位寄存器计算 `width * height`，再将低 32 位左移两位，写入
   `Entry::byteWeight`。这是确定的 modulo-2^32 回绕，不能在 C++ 中用可能
   发生有符号溢出的普通 `4 * width * height` 表示。

## 四端位置

| 目标 | `width` getter | `height` getter | 32 位乘法 / 左移 |
| --- | ---: | ---: | ---: |
| Android arm64 | probe `0x6A40FC`，value `0x6A4124` | probe `0x6A4160`，value `0x6A4188` | `0x6A4198` / `0x6A419C` |
| Android armv7 | `0x57A200` | `0x57A212` | `0x57A218` / `0x57A21C` |
| iOS arm64 | `0x1000FFC18` | `0x1000FFC30` | `0x1000FFC38` / `0x1000FFC3C` |
| iOS armv7 | `0xFCE78` | `0xFCE90` | `0xFCE9C` / `0xFCEA0` |

Android armv7、iOS arm64、iOS armv7 分别调用同构的
`ncbPropAccessor_getIntValue_guess`：

- Android armv7 `0x496B84`
- iOS arm64 `0x1000F9468`
- iOS armv7 `0xF651C`

它们的反编译结构均为：

```text
if (HasValue(name))
    return GetValue<tjs_int>(name)
return defaultValue
```

Android arm64 将外层 `HasValue` 分支内联在 bake 中；成功分支调用
`ncbPropAccessor_getValueInt_guess` (`0x6609BC`) 做第二次普通 PropGet 与
Variant→Integer 转换。`0x6A4108` / `0x6A416C` 都只按 probe 返回码的符号
选择默认 0 或第二次读取。

## 源码校正

原端口：

```cpp
entry.byteWeight = 4 * width * height;
```

在 `tjs_int == int32_t` 且乘积溢出时属于 C++ undefined behavior；即使当前
编译器通常恰好生成与 ARM 相同的 `MUL/LSL`，优化器并无义务保留回绕。
现改为：

```cpp
entry.byteWeight = multiplyW32(multiplyW32(width, height), 4);
```

`multiplyW32` 先在无符号低 32 位上运算，再以既有 `signedW32` 恢复相同
bit pattern，明确表达四端机器码语义。属性读取继续使用原 ncbind
`getIntValue(name, 0)`，因为其源码实现与四端 helper 一致。

## 回归覆盖

`SourceCache bakeSource reproduces low-blend scratch-layer calls` 现在额外记录
Layer 的 `PropGet` flags：

- 四个正常尺寸 entry：`width/height` 各一次 `MUSTEXIST` probe、一次普通
  value read；
- 缺失 `width`：只发生 probe，scratch 调用收到 width `0`；
- `width` probe 成功、第二次普通读取返回失败：两次调用都发生，最终
  scratch 调用仍收到 width `0`；
- `height` 在上述 entry 中仍保持完整的两次读取。

测试继续故意让动态绘制方法返回负错误码，以同时锁定 bake 调用链不消费
这些返回码的边界。
