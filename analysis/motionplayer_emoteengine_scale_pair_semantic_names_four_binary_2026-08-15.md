# MotionPlayer `EmoteEngine` scale pair 语义命名四参考复原（2026-08-15）

## 1. 结论

`EmoteEngine` 中 trigger-byte cluster 后的前两个 double 不是
`meshDivisionRatio`/`meshDivisionRatioDup`。四份当前参考闭合出两个独立语义：

```cpp
double metadataScale;
double inverseCombinedScale;
```

- `metadataScale` 从 metadata 的必需属性 `scale` 直接读取；
- `inverseCombinedScale` 始终按
  `1.0 / (metadataScale * currentScaleControllerOutput)` 计算；
- wind 创建和幅度更新只读取 `metadataScale` 作除数；
- shape-anchor resolution 只读取 `inverseCombinedScale` 作插值系数；
- Motion.Player、Motion.EmotePlayer、D3DEmotePlayer 的公开
  `meshDivisionRatio` 属性全部沿各自 wrapper 链进入内层 `Player` 的另一项 double，
  不读写这两个 Engine 字段。

旧本地名把 Android arm64 上偶然相同的 `Player+1176` 与 `Engine+1176` 数字当作同一
receiver，虽然运行路径已经逐步修正，错误字段名仍会误导新的调用链分析。本轮把源码和
测试统一改为 `_metadataScale` / `_inverseCombinedScale`；计算、顺序和边界行为不变。

## 2. 字段矩阵

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `metadataScale` | `+1168` | `+600` | `+800` | `+412` |
| `inverseCombinedScale` | `+1176` | `+608` | `+808` | `+420` |

四端 constructor 均把两项初始化为 exact `1.0`，其后紧跟同样为 `1.0` 的
hair/parts/bust scale triplet。字段物理 offset 只用于 IDB 定位；源身份由 reader/writer
集合判定。

## 3. fresh 函数定位

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_applyMetadata_guess` | `0x67A8B0` | `0x560020` | `0x1001B4468` | `0x1B3F58` |
| `EmoteEngine_stepRootControllers_guess` | `0x673AC0` | `0x55BFD4` | `0x1001AFD50` | `0x1AF4A4` |
| `EmoteEngine_resolveShapeAnchor_guess` | `0x678D50` | `0x55F098` | `0x1001B2C60` | `0x1B2774` |
| `EmoteEngine_setWind_guess` | `0x66DD8C` | `0x559900` | `0x1001AC718` | `0x1ABF24` |

## 4. writer 数据流

### 4.1 metadata commit

四端 `applyMetadata` 的共同顺序：

```text
apply mirror
reset controllers
Player progress bridge with dt=0
metadataScale = metadata["scale"].AsReal()
controllerScale = scaleController.step(0)
inverseCombinedScale = 1 / (metadataScale * controllerScale)
build remaining metadata controllers
```

| 动作 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| metadata `scale` get/store | `0x67A9B0/0x67A9B8` | `0x5600C8` | `0x1001B4524/0x1001B4528` | `0x1B406C` |
| initial reciprocal store | `0x67A9E0` | `0x5600F0` | `0x1001B4554` | `0x1B4098` |

required property get 的错误/Variant conversion 行为保持 native dispatch 路径；本轮不添加
missing-property fallback、zero check 或 finite check。

### 4.2 per-progress recompute

`stepRootControllers` 按 position -> color -> scale -> angle 顺序推进。scale step 完整写出
一个 float 后立即重算 reciprocal cache，再把同一个 scale float 送到 Player root zoom：

```cpp
inverseCombinedScale = 1.0 / (metadataScale * scaleOut);
player.setRootZoom(scaleOut, scaleOut);
```

四端 store 点为 `0x673B70 / 0x55C096 / 0x1001AFE08 / 0x1AF56C`。乘法先将
float 扩大到 double；除法没有 `scaleOut == 0`、NaN 或 infinity guard，所以正负零、
NaN 和 infinity 继续按 IEEE 规则传播。

## 5. reader 分离

### 5.1 wind 只读 metadata scale

`setWind` 在创建新 emitter 时把 normalized min/max 除以 `metadataScale`，更新 emitter
时把 absolute amplitude 除以同一值。代表性 load/reload 点：

| 目标 | create-path load | reuse-path reload |
|---|---:|---:|
| Android arm64 | `0x66DE3C` | `0x66DE94` |
| Android armv7 | `0x5599B8` | `0x559A84` |
| iOS arm64 | `0x1001AC7C8` | `0x1001AC89C` |
| iOS armv7 | `0x1ABFE6` | `0x1AC0B8` |

wind 不读取 reciprocal cache。metadata scale 为零或非有限值时也不防御；float narrowing、
比较和后续 emitter 行为按 native 顺序发生。

### 5.2 shape anchor 只读 inverse combined scale

`resolveShapeAnchor` 取得 shape `(x,y)` 和 root `(rootX,rootY)` 后加载 reciprocal cache，
保留 native X/Y crossover：

```text
outX = rootY + (shapeY - rootY) * inverseCombinedScale
outY = rootX + (shapeX - rootX) * inverseCombinedScale
```

四端 cache load 点为
`0x678F54 / 0x55F1A0 / 0x1001B2DE0 / 0x1B2926`。它不回读 metadata scale，也不
临时重算 reciprocal；因此 metadata replacement 或 controller step 的提交时序对下一次
shape resolution 可见。

## 6. 与公开 `meshDivisionRatio` 的 receiver 隔离

公开属性的真正字段位于 `Player`，四端 offset 是
`+1176/+832/+1064/+764`。Motion.EmotePlayer getter/setter 经过 Engine 的 Player owner，
D3DEmotePlayer 再多经过 shell/primary EmoteObject，但最终都读写该 Player double。
setter 是原始 load/store，不同步 `_metadataScale` 或 `_inverseCombinedScale`，也不置
Engine dirty。

所以本地测试必须同时保留两个不变量：

1. 三个 wrapper 的公开属性共享 inner Player raw scalar；
2. 任意公开边界值 round-trip 都不改变 Engine metadata/reciprocal pair。

## 7. 本地结果

- `EmoteEngine.h`：字段重命名为 `_metadataScale` 与 `_inverseCombinedScale`；
- `EmoteEngine.cpp`：metadata commit、root scale recompute 和 shape-anchor reader 改用
  语义名；`PlayerCore.cpp` 中定义的 Engine wind facade 同样改用 `_metadataScale`；
- unit translation unit：constructor defaults、reciprocal recompute 和 wrapper 隔离断言
  同步使用新名；
- `Player::_meshDivisionRatio` 及其三个公开 property surface 保持原名和原行为。

这是一项源结构/数据流命名修复，不改变二进制算法。绝对地址留在本文件和 recovery
IDB，不进入新的 compiled-source 注释或 helper 名。
