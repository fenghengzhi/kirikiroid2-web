# Geometry 标量 getters（四参考二进制，2026-08-26）

## 1. 范围

本纵切面闭合 Point/Circle/Rect 的八个标量 native binding：共享 `type`、共享
`x/y`、Circle `r`、Rect `l/t/w/h`。Quad `p` 返回 TJS Array/Dictionary，具有
独立 owner/hint 生命周期，不属于本标量任务。

## 2. 四端映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `type` | `GeometryShape_getType_guess@0x68E628` | `...@0x56E3FA` | `...@0x1000F08A4` | `...@0xECABE` |
| `x` | `PointCircle_getX_guess@0x68E630` | `...@0x56E3FE` | `...@0x1000F08AC` | `...@0xECAC2` |
| `y` | `PointCircle_getY_guess@0x68E638` | `...@0x56E408` | `...@0x1000F08B4` | `...@0xECACC` |
| `r` | `Circle_getR_guess@0x68E9DC` | `...@0x56E552` | `...@0x1000F09EC` | `...@0xECBBA` |
| `l` | `Rect_getL_guess@0x68EDE0` | `...@0x56E6B6` | `...@0x1000F0B4C` | `...@0xECCCA` |
| `t` | `Rect_getT_guess@0x68EDE8` | `...@0x56E6C0` | `...@0x1000F0B54` | `...@0xECCD4` |
| `w` | `Rect_getW_guess@0x68EDF0` | `...@0x56E6CA` | `...@0x1000F0B5C` | `...@0xECCDE` |
| `h` | `Rect_getH_guess@0x68EE00` | `...@0x56E6DC` | `...@0x1000F0B6C` | `...@0xECCF0` |

表中 32 个函数均在本轮以对应 `database` fresh decompile。原始二进制没有保留
完整 C++ getter 符号，因此语义名保留 `_guess`；角色由各类 member registrar
中的精确脚本名和绑定指针证明。四库已写入正确 `int/double` 返回签名并保存。

## 3. 共同源码伪代码

```text
getType(self): return self.type
getX(self):    return self.values[0]
getY(self):    return self.values[1]
getR(self):    return self.values[2]
getL(self):    return self.values[3]
getT(self):    return self.values[4]
getW(self):    return self.values[5] - self.values[3]
getH(self):    return self.values[6] - self.values[4]
```

没有 null guard、type gate、clamp、absolute-value、finite 检查、整数转换或临时
容器。`w/h` 的减法顺序分别严格是 right-left、bottom-top。

## 4. ABI 字段坐标

这些坐标只用于证明共同字段顺序，不进入 portable padding：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `type` | `+0` | `+0` | `+0` | `+0` |
| `values[0]` | `+8` | `+8` | `+8` | `+4` |
| `values[1]` | `+16` | `+16` | `+16` | `+12` |
| `values[2]` | `+24` | `+24` | `+24` | `+20` |
| `values[3]` | `+32` | `+32` | `+32` | `+28` |
| `values[4]` | `+40` | `+40` | `+40` | `+36` |
| `values[5]` | `+48` | `+48` | `+48` | `+44` |
| `values[6]` | `+56` | `+56` | `+56` | `+52` |

iOS armv7 的 double natural alignment 为 4；其余三端当前 record 的 double
起点为 8。所有端仍对应普通 `int32 + double[15]` 共享声明。

## 5. 边界行为

- direct getter 原样返回 NaN、Inf、subnormal 和 signed zero；
- `w/h` 按 IEEE subtraction 传播 NaN/Inf；
- 相同有限端点相减产生正零还是负零由原始操作数和目标 IEEE subtraction 决定，
  没有后续归一化；
- reversed rectangle 可返回负 `w/h`，没有修正。

## 6. 本地逐行对照

`cpp/plugins/motionplayer/SourceCache.h` 当前：

- `GeometryShapeBase_guess::getType()` 返回 `type`；
- Point/Circle `getX/getY/getR` 返回 `values[0..2]`；
- Rect `getL/getT` 返回 `values[3..4]`；
- Rect `getW/getH` 分别执行 `values[5]-values[3]`、
  `values[6]-values[4]`；
- `HitData` 使用自然 `int32 + std::array<double,15>`，并按 `alignof(double)`
  验证两种 ABI 起点。

本地实现逐项一致，本纵切面无需修改 C++。

## 7. 验证缺口

函数均为单读或单减法 leaf；四端 fresh decompile 是主要证据。当前机器缺少 CMake/
Emscripten，因此没有重新运行完整 motionplayer unit/Web build；最终审计仍需补跑。
