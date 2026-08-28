# Geometry shared `contains` 边界（四参考二进制，2026-08-26）

## 1. 四端映射与 fresh 证据

| 二进制 | shared body | fresh decompile | 边界反汇编 |
|---|---|---|---|
| Android arm64 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_68E1D0@0x68E1D0` | 已完成 | `FCMP/FCSEL/B.HI/B.LE/B.LS/CSET` 已核对 |
| Android armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_56E1B0@0x56E1B0` | 已完成 | `VCMPE/VMRS/VMOVLT/BHI/BLE` 已核对 |
| iOS arm64 | `Kirikiroid2_1.3.9_iOS_arm64!sub_1000F0670@0x1000F0670` | 已完成 | `FCMP/FCSEL/B.HI/B.LE/B.LS/CSET` 已核对 |
| iOS armv7 | `Kirikiroid2_1.3.9_iOS_armv7!sub_EC8D0@0xEC8D0` | 已完成 | `VCMPE/VMRS/VMOVLT/BHI/BLE` 已核对 |

四个 geometry NCB registrar 都把 `contains` descriptor 绑定到本表对应的同一个
body；Point/Circle/Rect/Quad 不各自复制算法。

## 2. 共享 record

四端共同读取：

```text
int32 type
ABI natural double alignment
double values[15]
```

LP64 与 Android armv7 的 `values` 起点为 `+8`；iOS armv7 的 ABI 对 double
只要求 4-byte alignment，因此起点为 `+4`。这是目标 ABI 布局差异；共享 C++
应使用自然字段声明，不能添加手工 padding。

## 3. 共同伪代码

```text
switch type:
case 1: // circle
    dx = x - values[0]
    dy = y - values[1]
    return dx*dx + dy*dy <= values[2]*values[2]

case 2: // rect
    if !(values[3] <= x): return false
    if !(x < values[5]): return false
    if !(values[4] <= y): return false
    return y < values[6]

case 3: // quad
    orientation =
        (values[12] - values[8]) * values[9]
      + (values[7] - values[11]) * values[10]
      - ((values[12] - values[8]) * values[7]
         + values[8] * (values[7] - values[11]))
    direction = orientation >= 0.0 ? +1.0 : -1.0

    for i in 0..3:
        current = 7 + 2*i
        next = 7 + 2*((i+1)&3)
        deltaY = values[next+1] - values[current+1]
        deltaX = values[current] - values[next]
        edge = deltaY*x + deltaX*y
             - (values[current]*deltaY
                + values[current+1]*deltaX)
        if direction*edge > 0.0:
            return false
    return true

default:
    return false
```

## 4. 浮点边界

### 4.1 circle

四端最终使用 `<=` 条件；unordered 为 false。半径为负时仍先平方，因此与同绝对
值正半径相同。边界点 inclusive。

### 4.2 rect

四端机器分支共同要求四个 ordered predicate：

```text
left <= x && x < right && top <= y && y < bottom
```

因此左/上 inclusive，右/下 exclusive；`x`、`y` 或任意一个 rect bound 为
NaN 时都返回 false。

当前本地代码使用：

```text
if (left > x || right <= x || top > y) return false
return bottom > y
```

它对普通有限数等价，也会在 `x/y` 为 NaN 时因最终比较返回 false；但
`left/right/top` 单独为 NaN、其它比较有序时可能错误返回 true。这是本轮确认的
边界偏差。

### 4.3 quad orientation

四端 orientation compare 后都使用 ARM `LT` 条件选择 `-1.0`：

- AArch64：`FCSEL minusOne, plusOne, LT`；
- ARMv7：默认 `+1.0`，随后 `VMOVLT -1.0`。

VFP/AArch64 `FCMP` 的 unordered flags 使 `LT` 成立。因此 orientation 为 NaN
时四端都选择 `-1.0`。这与当前本地
`orientation < 0.0 ? -1.0 : 1.0` 的标准 C++ NaN 结果 `+1.0` 不同；联合
控制流对应 `orientation >= 0.0 ? +1.0 : -1.0`。

edge product 为 NaN 时，四端的 reject 条件 `> 0.0` 为 false，继续下一条 edge；
这与普通 C++ ordered `>` 一致。

## 5. 四端差异

- Android arm64 Hex-Rays 把 orientation 写成“默认 -1，`>=0` 改 +1”；其它
  三端写成“默认 +1，`<0` 改 -1”。机器条件码证明 unordered 时四端结果相同，
  伪代码文本差异是 decompiler 表达差异，不是源码/版本差异。
- Android armv7 与 iOS armv7 的参数/record 偏移因 double alignment 不同；
  算术和分支顺序一致。
- 四端 quad 都循环恰好四条边，next index 使用 `(i+1)&3`；没有容器或动态长度。

## 6. 本地逐行修正结果

`cpp/plugins/motionplayer/HitTestInternal.h` 已只做两个证据支持的语义修正：

1. Rect 保留逐条件早退和比较顺序，但改为 ordered-positive predicate 的否定，
   使任一 bound NaN 都拒绝；
2. Quad direction 改为 `orientation >= 0.0 ? 1.0 : -1.0`，复刻四端
   unordered 选择。

Circle、edge expression、循环顺序、record 字段和 default false 未修改。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 已补：

- rect 的 left/right/top/bottom 分别为 NaN；
- quad orientation 为 NaN 时 direction 为 -1 的可观察用例；
- 保留现有有限数 differential case 作为非回归。

本轮还把四端共享 body 命名为 `GeometryShape_contains_guess`。二进制证明了
Point/Circle/Rect/Quad 共享该 body，但没有精确基类源码名，因此按命名规则保留
`_guess`；四库函数头均写入边界注释并保存。

## 7. 验证

- `git diff --check` 通过；
- 使用系统 Apple Clang 21 直接包含当前 `HitTestInternal.h` 编译 C++17 临时
  harness，`-Wall -Wextra -Werror` 通过；
- harness 覆盖 circle、rect inclusive/exclusive、四个 rect bound NaN 和 quad
  orientation NaN，运行通过；
- 当前机器没有 `cmake`、Ninja、Emscripten SDK，也没有已有 `out/` 构建目录，
  因此正式 motionplayer unit target、Wasmtime differential 和 Web build 尚未运行。

后者是明确保留的验证缺口，不影响四端证据充分后的忠实实现，但在最终恢复审计前
必须于具备项目工具链的环境补跑。
