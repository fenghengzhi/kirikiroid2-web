# MotionPlayer prepared-item Bezier division 生成边界（四参考二进制，2026-08-14）

## 1. 结论

prepared-render-item builder 不会把节点的 `meshDivision` 原样复制到 item。四份
参考二进制共同执行：

```text
node.meshDivision raw low 32 bits
        -> interpret as uint32
        -> exact double conversion
        * Player.meshDivisionRatio raw double
        -> signed int32 conversion, round toward zero, native saturation
        -> integer min(converted, 50)
        -> persistent PreparedRenderItem.commandPatchDivision
```

这和随后 `getCommandList` 的转换是两个独立阶段：

```text
prepared = min(sat_i32(ratio * uint32(node.meshDivision)), 50)
command  = (ratio * prepared is ordered < 50)
             ? sat_i64(ratio * prepared)
             : 50
```

所以 ratio 会在 command-query 数据流中再次参与乘法，而不是仅乘一次。例如
`ratio=2, meshDivision=4` 先得到 prepared `8`，再序列化为 command `16`；
`ratio=-2` 先得到 `-8`，第二次乘法则得到 `+16`。这不是 Web 重构产生的平方，
四端两个函数各自都有独立 ratio load 和 multiply。

旧 Web 源码存在两个边界错误：

- `node.meshDivision` 的同一 32 位字样按 signed `int` 转 double，参考实现按
  unsigned 转换；
- product 直接 `static_cast<int>`，NaN 和 signed-int32 越界进入 C++ UB。

本次用 `prepareBezierPatchDivision_guess` 显式表达 raw unsigned input、signed
saturation 和 conversion 后的整数 cap。

## 2. 分支位置与持久字段时序

该计算位于四端同一个 recursive prepared-item builder：

| 目标 | builder | ratio load / conversion block |
|---|---:|---:|
| Android arm64 | `Player_appendPreparedRenderItems_guess` | `0x6BFAB4..0x6BFAE0` |
| Android armv7 | 同一 recovered builder | `0x58B66A..0x58B690` |
| iOS arm64 | 同一 recovered builder | `0x100114EF0..0x100114F18` |
| iOS armv7 | 同一 recovered builder | `0x112906..0x112930` |

进入该 block 前的容器/门顺序也是四端一致：

1. 先把 node composite mesh vector assign 到 persistent item 的独立 vector；
2. composite vector nonempty：把 item mesh type 改为 2，完全跳过 Bezier block；
3. composite empty 且 node mesh type 不等于 1：同样跳过；
4. node mesh type 等于 1、raw mesh-control vector empty：把 item mesh type 改为 0，
   跳过 division 和两个 Bezier vector assignment；
5. 只有 raw mesh-control vector nonempty 才写 `commandPatchDivision`，随后依次
   assign processed mesh vector 和 raw command patch vector。

item 是 node 持久拥有并反复复用的对象；跳过 Bezier block 不会额外清零旧
`commandPatchDivision`。raw-control empty 分支也不会顺手清空两个旧 Bezier vector。
这些 stale bytes/owners 因 mesh type 被改写而通常不被当前分支消费，但它们仍是对象
生命周期的一部分，不能用“每帧新建零初始化 item”解释。

division 写入发生在两个 vector assignment 之前。因此后续 vector allocation/copy
抛异常时，新 division 已发布；若第一个 vector assignment 成功而第二个失败，processed
vector 和 division 都保留新值，没有事务回滚。

## 3. 指令证据

### 3.1 Android arm64

```asm
0x6BFAB8  LDR     S0, [X19,#0x7D8]   ; node meshDivision word
0x6BFAC4  LDR     D1, [X9,#0x498]    ; Player.meshDivisionRatio
0x6BFAC8  UCVTF   D0, D0             ; uint32 -> double
0x6BFAD0  FMUL    D0, D1, D0
0x6BFAD4  FCVTZS  W9, D0             ; double -> signed int32
0x6BFAD8  CMP     W9, #50
0x6BFADC  CSEL    W9, W9, W10, LT    ; W10 = 50
0x6BFAE0  STR     W9, [X8,#0x170]
```

### 3.2 Android armv7

```asm
0x58B66E  VLDR        S0, [R1]
0x58B672  VCVT.F64.U32 D0, S0
0x58B67A  VLDR        D1, [R1,#0x340]
0x58B67E  VMUL.F64    D0, D1, D0
0x58B682  VCVT.S32.F64 S0, D0
0x58B686  VMOV        R1, S0
0x58B68A  CMP         R1, #50
0x58B68C  IT          GE
0x58B68E  MOVGE       R1, #50
0x58B690  STR         R1, [item,#0x128]
```

### 3.3 iOS arm64

```asm
0x100114EF4  LDR     S0, [X9,#0x7E8]
0x100114EF8  UCVTF   D0, D0
0x100114F00  LDR     D1, [X10,#0x428]
0x100114F04  FMUL    D0, D0, D1
0x100114F08  FCVTZS  W10, D0
0x100114F0C  CMP     W10, #50
0x100114F14  CSEL    W10, W10, W11, LT
0x100114F18  STR     W10, [X8,#0x170]
```

### 3.4 iOS armv7

```asm
0x11290E  VLDR        S0, [R0]
0x112912  VCVT.F64.U32 D16, S0
0x11291A  VLDR        D17, [R0,#0x2FC]
0x11291E  VMUL.F64    D16, D16, D17
0x112922  VCVT.S32.F64 S0, D16
0x112926  VMOV        R0, S0
0x11292A  CMP         R0, #50
0x11292C  IT          GE
0x11292E  MOVGE       R0, #50
0x112930  STR         R0, [item,#0x128]
```

两个 AArch64 目标用 `FCVTZS W,D`，两个 ARMv7 目标用
`VCVT.S32.F64`；这里没有 ARMv7 外部 helper，所以 NaN/溢出边界全部由插件内
指令直接证明。

## 4. 数值边界

native signed conversion 先向零取整，并把无效/越界输入落到 signed-int32
边界：

| product | converted | prepared item field |
|---|---:|---:|
| NaN（包括 `0 * ±Inf`） | `0` | `0` |
| `+Inf` 或 `>= 2^31` | `INT32_MAX` | `50` |
| `-Inf` 或 `<= -2^31` | `INT32_MIN` | `INT32_MIN` |
| finite in-range | 向零截断 | converted `< 50` 时原值，否则 `50` |

cap 是 signed **整数** comparison，不是对原 product 的浮点 comparison。因此：

- NaN 由 conversion 变成 0 后保留 0；
- 所有负数，包括 `INT32_MIN`，都绕过上限 cap；
- `49.999` 先截成 49，得到 49；
- `50.0`、`50.999` 和更大有限正数最终都是 50。

node input 的 unsigned 解释也可观察：raw `0xFFFFFFFF` 先成为
`4294967295.0`，而不是 `-1.0`。ratio 为 `1` 时正溢出后 cap 为 50；ratio
为 `-1` 时负溢出饱和为 `INT32_MIN`。

## 5. 与 `getCommandList` 的组合边界

[`motionplayer_get_command_list_division_conversion_four_binary_2026-08-14.md`](motionplayer_get_command_list_division_conversion_four_binary_2026-08-14.md)
恢复了第二阶段的 signed-int64 conversion 和 ordered-`<50` select。两阶段组合
会产生几个并不直观、但由四端数据流共同决定的结果：

| ratio / raw division | prepared field | command Dictionary field |
|---|---:|---:|
| `2 / 4` | `8` | `16` |
| `0.5 / 7` | `3` | `1` |
| `-2 / 4` | `-8` | `16` |
| `NaN / 4` | `0` | `50`（`NaN * 0` unordered） |
| `+Inf / 0` | `0` | `50`（两次均产生 NaN） |
| `-Inf / 4` | `INT32_MIN` | `50`（第二次 product 为 `+Inf`） |

prepared item 的其他渲染消费者读取第一阶段字段，而 TJS command query 读取第二
阶段字段；两者不能共享一个“规范化 division”helper。

## 6. 源码与测试

`prepareBezierPatchDivision_guess(double, uint32_t)` 现在：

1. 以 uint32 接收 raw node bits；
2. 执行 double product；
3. 显式处理 NaN、`±2^31` 边界和 signed saturation；
4. 最后做 signed integer `>= 50` cap。

call site 对仍以 `int` 保存的 Web node 字段先做 `static_cast<uint32_t>`，保留
低 32 位而不扩大本纵切面到 parser/结构体全局类型重写。

测试覆盖正常正负/小数 ratio、49/50 边界、raw `UINT32_MAX`、NaN、正负无穷、
`Inf * 0` 和负向 signed-int32 溢出。

## 7. IDB 与验证

- 四份 recovery IDB 的 unsigned input conversion、signed output conversion 和
  integer cap 站点均已写入语义注释并保存；
- 完整测试 TU `-fsyntax-only` 已通过，只有仓库既有 `_tss` warning；
- `Web Debug Build` 已成功重编 motionplayer、生成静态库并完成最终 Wasm/HTML
  链接；
- `git diff --check` 退出码为 0；输出只有工作树既有 LF/CRLF 转换提示。
