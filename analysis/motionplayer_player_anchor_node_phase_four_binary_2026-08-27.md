# Player anchor-node phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 四端函数族

### 1.1 phase root

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BD908` | 498 |
| Android armv7 | `0x589C00` | 528 |
| iOS arm64 | `0x100113024` | 437 |
| iOS armv7 | `0x110908` | 552 |

### 1.2 transform-order local-matrix helper

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x696D20` | 105 |
| Android armv7 | `0x572F80` | 129 |
| iOS arm64 | `0x1000F6A7C` | 106 |
| iOS armv7 | `0xF36BC` | 131 |

八个函数均 fresh decompile，并完整读取 disassembly，cursor 全部 `done=true`。anchor 是 phase3最后
一个子阶段；root没有preview gate，也没有child递归或particle Array。

## 2. 遍历、assign-images 与 source publication

函数按physical deque order遍历 `nodes[1..end)`，只处理 `nodeType==10 && accumulated.active`。
每个命中node先把Player `_needsInternalAssignImages=true`；这一写入发生在所有后续快捷返回之前。

`_deltaTime == 0.0` 或 internal render layer未ready时，只把 `anchor.source.valid=false`并continue；
source.object、尺寸、origin和clip旧值全部保留，`-0.0`同样命中。

live路径retain internal layer dispatch，按下列顺序发布：

1. source.object Variant CopyRef；
2. source.valid=true；
3. 读取width Integer并转double；
4. 读取height Integer并转double；
5. origin为width/height的一半；
6. clip写 `(0,0,1,1)`。

property status失败使用0；Variant/object转换和callback异常直接传播。valid在尺寸读取前已发布，因此
width异常会留下object+valid的partial state。临时layer receiver在进入阻尼计算前释放。

## 3. dampPower 与 transform

原版没有把阻尼指数做代数化简，而是保持：

```text
scaledDelta = deltaTime / speedMul
dampPower = deltaTime
          * (scaledDelta * deltaTime / scaledDelta)
          / scaledDelta / 60.0 / feedbackTimespan
```

因此speedMul、feedbackTimespan或delta的零、负、NaN、Inf会经过多个独立IEEE操作，不能替换成
看似等价的简式。

angle `<180`时直接乘dampPower；否则执行
`360 - (360-angle)*dampPower`，不循环规范化。scale X/Y分别把当前值乘32、除source尺寸后做pow；
slant X/Y直接乘dampPower。

随后transform-order helper在enabled count非零时把node 2x2重置identity，并严格按四个stored op code
依次执行flip、angle、zoom、slant；unknown op忽略。angle使用固定PI常量和原版乘加分组，translation
不参与。anchor root把helper输出写回accumulated matrix。

`_independentLayerInherit=false`时再用synthetic root accumulated 2x2左乘anchor local matrix；为真时
保留local matrix。root index、transformOrder和所有node字段按native未检查边界消费。

## 4. opacity 与position

opacity读取当前int为unsigned 32-bit后除255；零改用`1/255`。结果为：

```text
clamped = clamp_zero_fallback(pow(normalized,dampPower)
                              * 255 * anchorOpaScale, 0, 255)
opacity = int_toward_zero(clamped)
anchorOpaScale = clamped / converted_truncated_denominator
```

opacity clamp对NaN回退0；scale更新保留零除、signed/unsigned conversion残差。position三轴以root为
固定点：`root + dampPower*(anchor-root)`，没有finite guard。

## 5. packed color 分类与跨node残留

函数栈上有一个故意不初始化、也不逐node重置的RGB base。它只在若干已处理node后得到值，所以
首个命中特定分支的anchor可读取indeterminate值；本地保留相同source-level边界。

先比较四个packed colors：

- 不全等：default blend使用128，否则255；处理四组颜色；
- 全等、非default：base=255；全白 `0xFFFFFFFF` 直接continue且不更新carry；
- 全等、中灰 `0xFF808080`：把当前base写carry后continue；
- 其它全等：只处理第一组，再把第一packed word复制到后三组；
- 全等且default blend：base取上一已携带值，可能未初始化。

default判定只看active slot blendMode高nibble是否`0x10`。early-continue发生在opacity、position与matrix
已经提交之后，只有颜色/携带值路径被截断。

## 6. 颜色阻尼的原版不对称

每组颜色读取byte顺序2、1、0、3，对应scale顺序0、1、2、3；结果写byte顺序0<-channel2、
1<-channel0、2<-channel1、3<-alpha。RGB以base归一化，零输入改1；alpha固定以255归一化，零改
`1/255`。

颜色channel clamp与opacity相反：NaN回退255。更反直觉的是scale 1除以channel0截断结果的
unsigned denominator，而scale 0除signed denominator；scale 2/3也走unsigned correction。零除、
负值转换、NaN和Inf都不修复。处理完成后把本node使用的rgbBase写入carry供后续node消费。

这些byte/scale交叉关系和carry生命周期在四个优化参考中一致，不应“整理”为正常RGBA循环。

## 7. owner、异常与本地对照

phase的持久owner变化只有anchor source.object对internal layer的Variant引用；临时receiver覆盖width/
height两次property读取。其余操作是node内原地写入。异常按上述publication顺序留下partial state，
没有回滚。

`PlayerUpdateAnchor.cpp` 的gate、source owner、未化简dampPower、矩阵、opacity、position、颜色分类、
carry与asymmetric channel bugs逐项匹配；`applyLocalTransform` helper保留四步operation order和浮点结合。
本轮无需修改编译语义。四库已统一命名root/helper，追加注释、bookmark并保存。

## 8. 验证限制

已有helper用例覆盖opacity/color clamp、零分母残差与channel/scale交叉写入。已执行coverage严格12列、
duplicate-ID检查和 `git diff --check`。当前环境缺少正式CMake/Ninja/Emscripten依赖工具链，不能
声称unit/Web build通过。
