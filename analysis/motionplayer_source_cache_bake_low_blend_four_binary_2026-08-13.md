# SourceCache::bakeSource 低 blend 缓冲合成四端复核（2026-08-13）

> **V184 勘误（2026-08-17）**：本报告的调用顺序、参数和 ignored-HRESULT 结论仍成立，
> 但原版并非向这些 `FuncCall` 传 null result。四端 fresh call-site ABI 证明，`drawLayer` 前
> 构造的同一个非空 Void `tTJSVariant` 被六个调用复用，普通失败不会清空它，最后才析构。
> 完整地址、五槽 hint family 与生命周期证据见
> `analysis/motionplayer_source_cache_bake_shared_result_hint_family_boundary_four_binary_2026-08-17.md`。

## 范围与结论

本轮只复核 `SourceCache::bakeSource` 中 packed corner tint 之后的低四位
blend 分支，以及该分支使用的 `__Private_Motion_GLLayer` native-instance
查询。证据来自当前 `reference/binaries/` 的四个目标，而不是旧
`libkrkr2.so` 注释：

| 目标 | bake helper | 独立 native 查询 helper | 私有类注册 / ClassID 写入 |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6A3FC0` | 在 `0x6A41E8..0x6A4230` 内联 | `0x6DA664` / `0x6DA6E8` |
| Android armv7 | `0x57A168` | `0x57AA80` | `0x59BD98` / `0x59BDF2` |
| iOS arm64 | `0x1000FFB24` | `0x1001006C0` | `0x10012A73C` / `0x10012A7A8` |
| iOS armv7 | `0xFCD68` | `0xFD7E8` | `0x1293D4` / `0x129490` |

四端共同结论：

1. 先无条件执行 `source.drawLayer(entry.layer)`，读取 Layer 的
   `width/height`，写入 `byteWeight = 4 * width * height`，再应用 corner
   tint。
2. 只有 `(blendMode & 0x0f) - 1 < 2` 的无符号比较成立，即低四位恰为
   `1` 或 `2` 时，才考虑 scratch Layer 合成。
3. 四端都调用带函数局部静态缓存的
   `TVPIsSoftwareRenderManager()` 判定渲染后端，不是每次直接执行
   `TVPGetRenderManager()->IsSoftware()`。软件渲染直接进入 scratch 路径。硬件渲染先用私有
   `__Private_Motion_GLLayer` ClassID 查询 entry Layer；返回的 native
   指针非空时跳过 scratch 路径，为空时进入。
4. scratch 路径严格按
   `bufLayer.setSize -> bufLayer.copyRect -> layer.fillRect ->
   layer.operateRect` 执行；低四位为 `2` 时最后再执行
   `layer.adjustGamma`。
5. 上述所有动态 `FuncCall` 的 `tjs_error` 返回值均未被检查。负错误码
   不会中断后续调用；六次调用复用同一个非空 result Variant，调用间不清空，因此失败但
   不写 result 时会保留前一次值。C++/TJS 抛出的异常仍按正常异常机制向外传播。
6. `fillRect` 的颜色不是有符号 32 位 `-16777216`，而是 Integer
   `+4278190080`（`0x00000000FF000000`）。四端 64 位 payload 都将高
   32 位清零。

## 精确调用序列和参数

四端的调用边界一致：

```text
dispatchResult = Void Variant
source.drawLayer(entry.layer)                         // 1 arg, &dispatchResult
bufLayer.setSize(width, height)                       // 2 args, &dispatchResult
bufLayer.copyRect(0, 0, entry.layer, 0, 0,
                  width, height)                      // 7 args
entry.layer.fillRect(0, 0, width, height,
                     Integer(+4278190080))             // 5 args
entry.layer.operateRect(0, 0, bufLayer, 0, 0,
                        width, height, 15)             // 8 args
if ((blendMode & 0x0f) == 2):
    entry.layer.adjustGamma(1, 255, 0,
                            1, 255, 0,
                            1, 255, 0)                 // 9 args
```

`copyRect/fillRect/operateRect/adjustGamma` 也都接收上面的 `&dispatchResult`；注释只在前两行
展开，避免遮蔽既有参数表。result 在调用间保持存活，并在最后一个 Layer accessor 之后析构。

关键 call site：

| 调用 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| 低 blend gate / native query | `0x6A41DC` | `0x57A25A` | `0x1000FFC90` | `0xFCEEE` |
| `setSize` | `0x6A42E0` | `0x57A2C8` | `0x1000FFD14` | `0xFCF68` |
| `copyRect` | `0x6A439C` | `0x57A358` | `0x1000FFDCC` | `0xFD000` |
| `fillRect` | `0x6A445C` | `0x57A3E4` | `0x1000FFE8C` | `0xFD09C` |
| `operateRect` | `0x6A4534` | `0x57A47E` | `0x1000FFF70` | `0xFD150` |
| mode 2 gate | `0x6A4594` | `0x57A4C2` | `0x1000FFFD0` | `0xFD19A` |
| `adjustGamma` | `0x6A4654` | `0x57A542` | `0x1001000A0` | `0xFD22C` |

Android arm64 在 `0x6A43D8..0x6A43DC` 直接构造
`4278190080LL`。iOS arm64 在 `0x1000FFE30` 同样写入该正数；两个
32 位目标分别在 `0x57A3AA..0x57A3AE`、`0xFD050..0xFD058` 写入低字
`0xFF000000`、高字 `0`。因此源代码必须用 `tjs_int64` 构造该
Variant，不能先窄化为 `tjs_int`。

## 私有 native-instance 查询的边界

Android armv7 `0x57AA80`、iOS arm64 `0x1001006C0`、iOS armv7
`0xFD7E8` 是同构 helper；Android arm64 在 bake 内联相同逻辑：

```text
object = strict Variant-to-Object conversion(entry.layer)
native = nullptr
object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                              PrivateMotionGLL_ClassID,
                              &native)
return native
```

边界细节：

- Variant 类型不是 Object 时，严格转换 helper 抛转换错误。
- typed-null Object 通过类型转换后得到 null；原实现仍立即读取 vtable，
  没有 null 恢复分支。
- 输出槽在调用前明确清零。
- `NativeInstanceSupport` 的 `tjs_error` 返回值完全丢弃。一个恶意或特殊
  dispatch 可以“返回失败但写出非空 native”，helper 仍返回该非空值。
- ClassID 来自同一个私有 native class 注册器。四端注册器均把注册结果
  同时写入 class 对象自身的 ClassID 字段和上述全局，再注册 constructor、
  `setSize`、`visible`、`absolute`。

此前端口把 SourceCache 的这条查询复用了 raw-object
`resolvePrivateMotionGLLNativeLike_0x6DE24C`。四端证据实际表明这里存在接收
Variant 的独立 helper；本轮据此拆出
`queryPrivateMotionGLLNativeFromVariant_guess`，只在 SourceCache 的 bake 与
tint call site 使用。“输出初始化为空、调用、忽略状态、返回输出”的边界
不会被误扩散到 PrivateMotionGLL 自身 constructor/setSize/队列 callback；
后者继续保留原 raw-object 解析器，等待对应四端证据单独复核。

## 源码和回归落点

- `cpp/plugins/motionplayer/PrivateMotionGLL.cpp`
  - 新增 SourceCache 专用 Variant 查询：严格转换、无 typed-null 恢复、
    忽略 `NativeInstanceSupport` 返回码；
  - raw-object 解析器不受本轮局部证据影响。
- `cpp/plugins/motionplayer/SourceCache.cpp`
  - bake 与 corner-tint 的软件/硬件分流均使用带局部静态缓存的
    `TVPIsSoftwareRenderManager()`；
  - `fillRect` 的 ARGB 参数改由 `tjs_int64(0xFF000000u)` 构造，保留
    `+4278190080`；
  - V184 又恢复函数级 result Variant，并让 drawLayer 与五个 scratch method 复用其地址。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 增加失败返回码但写出非空 native 的 query probe；
  - 增加低 blend 0/1/2/3 的完整序列测试；
  - 逐项验证参数数量、Object/ObjThis、尺寸、mode 15、gamma 九元组以及
    `fillRect` 的完整 64 位 Integer；
  - probe 故意让 `drawLayer` 与 scratch 方法均返回 `TJS_E_FAIL`，用于
    锁定原调用链忽略错误码并继续执行的行为；V184 进一步逐调用验证同一 result pointer、
    前一值可见以及失败后不清空。

## IDB 改进

四端私有 ClassID 全局已命名为 `PrivateMotionGLL_ClassID_guess`；三个独立
helper 已命名为 `Variant_getPrivateMotionGLLNative_guess`，Android arm64
内联点添加了等价注释。四端 bake gate、颜色 payload 和调用序列均已添加
行注释及 bookmark。所有名称带 `_guess`，避免把尚未恢复的原始 C++
符号误写成确定事实。
