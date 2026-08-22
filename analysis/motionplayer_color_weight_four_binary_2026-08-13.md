# MotionPlayer `colorWeight` 四参考垂直复核（2026-08-13）

## 1. 结论

四份当前参考二进制共同给出一条完整的颜色权重数据流：

```text
Player 构造
  internal packed RGBA = 0xFF808080
        │
        ├─ NCB colorWeight getter/setter
        │    script packed word ⇄ byte0/byte2 swap ⇄ internal packed RGBA
        │
        ├─ EmoteEngine 根颜色控制器
        │    四个控制值组装成 script word → Player setter
        │
        └─ prepared-render-item 构造
             inherited weight × Player weight
                  ├─ 按 inheritFlags 0x200 递归给 type-3/type-4 子 Player
                  └─ 分别乘入节点四个累计顶点色 → source clip 重映射
```

中性元为 `0xFF808080`：三个 RGB 通道使用以 128 为 1.0 的权重域，alpha
使用普通 255 域。非中性组合带一个进程级、单项、无序输入对缓存。缓存是
无锁的；中性元快速路径不读写缓存。

本次还纠正了一个会污染后续逆向的旧映射：Android arm64 的
`0x6C2208` 是后续 render-command materialization，第 4 参数是渲染上下文/
裁剪指针；真正的递归 prepared-render-item 构造器是 `0x6BF714`。旧源码注释
和探针把前者内部的 `0x6C2334` 当成函数入口，属于从旧 `libkrkr2.so` 沿用的
错误地址。独立的递归 `calcBounds` 仍是四参考 bounds 垂直确认的 `0x6C10E4`。

## 2. 属性字符串与 NCB 绑定

本轮从四份 IDB 重新以 UTF-16LE 精确搜索 `colorWeight`，没有用旧注释反推：

| 目标 | 字符串 | Player NCB 注册函数 | 属性注册点 | getter | setter |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x14D648C` | `0x6D3DA8` | `0x6D4B20` | `0x6CAAF0` | `0x6CAB04` |
| Android armeabi-v7a | `0xD85D9A` | `0x597EC8` | `0x598214` | `0x5928F4` | `0x59290C` |
| iOS arm64 | `0x10195CBB0` | `0x1001244F8` | `0x1001249C8` | `0x10011D4E4` | `0x10011D4F8` |
| iOS armv7 | `0x174EF14` | `0x123848` | `0x123CC0` | `0x11BE94` | `0x11BEAC` |

四端 getter/setter 的位运算完全一致：

```cpp
swap(p) = (p & 0xFF00FF00)
        | ((p >> 16) & 0xFF)
        | ((p & 0xFF) << 16);
```

即 byte 0 与 byte 2 交换，byte 1 与 byte 3 原样保留。该变换是 involution，
所以任意 32 位脚本位型都能 `set` 后由 `get` 原样读回，包括最高位为 1、全零、
全一等值。setter 直接写字段：没有相等短路、范围验证、dirty 标记或递归传播。

## 3. 字段布局、构造默认值与根控制器写入

| 目标 | Player 字段偏移 | 构造函数 | 默认值写入 | 根控制器调用 setter |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `+0x484` | `0x6CC110` | `0x6CC4D0` | `0x673B40` |
| Android armeabi-v7a | `+0x32C` | `0x5935C4` | `0x59382C` | `0x55C066` |
| iOS arm64 | `+0x414` | `0x10011EC04` | `0x10011EE6C` | `0x1001AFDDC` |
| iOS armv7 | `+0x2E8` | `0x11D488` | `0x11D864` | `0x1AF540` |

四个构造函数都写入 `0xFF808080`。根控制器 bridge 不是绕过属性直接写字段，
而是调用同一个 setter，因此控制器组装的脚本通道顺序会在进入渲染存储时完成
同一 R/B 交换。

节点生成的子 motion 和粒子 child 则处在 ABI 的另一侧：它们把节点已有的
render-native 四字节值直接写进子 Player 字段，不再交换一次。这解释了为何
脚本 setter 与内部 child 传播不能共用一个表面上相同的赋值入口。

## 4. prepared-render-item 构造器纠错

| 目标 | 递归构造器 | 外层 content-gate/build/sort wrapper |
|---|---:|---:|
| Android arm64-v8a | `0x6BF714` | `0x6D2544` |
| Android armeabi-v7a | `0x58B178` | `0x596DF0` |
| iOS arm64 | `0x1001148F8` | `0x100122F68` |
| iOS armv7 | `0x1123D8` | `0x121FDC` |

三份非 Android-arm64 构造器在入口立刻执行：

```text
effective = multiply(inherited, Player.colorWeightPacked)
```

随后在创建普通 render item 时，对节点的四个 packed corner color 分别执行：

```text
item.color[i] = multiply(node.color[i], effective), i = 0..3
```

Android arm64 在 `0x6BF714..0x6BF83C` 内联第一次组合，在
`0x6C0884..0x6C0958` 的四次循环内联第二次组合，数据流一致。type-3/type-4
递归时，节点 `inheritFlags & 0x200` 为真才把 `effective` 传给子 Player；否则
传中性元。外层 wrapper 均先检查 motion-content，再以中性元和两个 false
lineage flag 调递归构造器，最后稳定排序主 render-item 指针向量。

## 5. 乘法、饱和与单项缓存

| 目标 | 乘法实现 | cache A | cache B | cache result |
|---|---:|---:|---:|---:|
| Android arm64-v8a | 内联于 `0x6BF714` | `0x1AB5568` | `0x1AB556C` | `0x1AB5570` |
| Android armeabi-v7a | `0x58BD1C` | `0x11119D8` | `0x11119DC` | `0x11119E0` |
| iOS arm64 | `0x1001156D8` | `0x101B69A08` | `0x101B69A0C` | `0x101B69A10` |
| iOS armv7 | `0x113040` | `0x187D698` | `0x187D69C` | `0x187D6A0` |

精确控制流为：

```text
if rhs == 0xFF808080: return lhs
if lhs == 0xFF808080: return rhs
if cache == (lhs,rhs) or cache == (rhs,lhs): return cachedResult

out.r = min(255, (lhs.r * rhs.r) >> 7)
out.g = min(255, (lhs.g * rhs.g) >> 7)
out.b = min(255, (lhs.b * rhs.b) >> 7)
out.a = (lhs.a * rhs.a) / 255
cache = (lhs, rhs, out)
return out
```

因此组合是交换律的；缓存也显式接受倒序输入。RGB 最大乘积 `255*255` 右移
7 位得到 508，随后饱和到 255；alpha 是截断整数除法。缓存只是一项性能状态，
不参与中性快速路径，也没有互斥或原子操作。便携实现现在保留这三个进程级
标量和相同读写顺序，而不是只复原纯函数结果。

## 6. 源码与 IDB 落地

源码侧完成：

- getter/setter helper 改为无旧地址的 `swapPackedRedBlue_guess`；
- packed multiply 改为 `multiplyPackedColorWeights_guess`，恢复单项无序对缓存；
- Player/NCB/child-propagation 注释改写为四参考语义，不再声称旧
  `libkrkr2.so` 偏移；
- Android oracle 的 prepared-render wrapper/build-items 偏移改为当前
  `0x6D2544`/`0x6BF714`，trace sample point 改用语义名；
- evaluator 文档中的 Android arm64 prepared-render 构造器映射同步纠正；
- 新增任意脚本位型 round-trip、中性元、RGB 饱和、alpha 截断、交换律/倒序
  cache-hit 的测试。

四份 IDB 均已把 getter/setter、递归构造器、乘法 helper、wrapper 恢复为
`_guess` 语义名，并写入字段通道、缓存、乘法和 wrapper 生命周期注释后原位保存。

## 7. 边界声明

本闭环只宣称颜色权重与 prepared-render 入口身份已经四端闭合。缓存的原生
无锁访问在 C++ 多线程内存模型下形成 data-race 风险；当前二进制没有同步，
便携实现为保持边界也不擅自加锁。此处不据此推断整个渲染器允许多线程并发调用。

同样，本记录不把尚未重新映射的旧渲染执行、draw-affine 或 staged diagnostic
地址自动视为当前地址；它们仍须各自做四参考垂直复核。

## 8. 验证

- `Web Debug Build`：成功，重新编译并链接 motionplayer 与 `index.html`。
- `Wasmtime Headless Debug Build`：成功，包含 guest objects 与最终链接。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：使用 Web 目标的真实 include、
  define、ABI 与 Emscripten 参数执行整翻译单元 `-fsyntax-only`，成功；只有项目
  既有的 `_tss` 弃用警告。
- `node --check tests/differential/oracle_runner/frida_motion_stage_agent.js`：
  成功。
- `git diff --check`：成功；PowerShell 仅报告工作树既有的 LF→CRLF 提示。
