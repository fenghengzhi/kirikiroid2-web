# Player updateLayers phase1/phase2 四参考二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的函数分母

### 1.1 updateLayers roots（phase1/phase2内联）

| 平台 | root | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6B871C` | 685 |
| Android armv7 | `0x5856E0` | 764 |
| iOS arm64 | `0x10010E544` | 719 |
| iOS armv7 | `0x10BE5C` | 821 |

### 1.2 timeline evaluator

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x696EC4` | 634 |
| Android armv7 | `0x573158` | 631 |
| iOS arm64 | `0x1000F6C34` | 585 |
| iOS armv7 | `0xF3894` | 750 |

### 1.3 variable-track interpolation

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6B9200` | 120 |
| Android armv7 | `0x5860BC` | 112 |
| iOS arm64 | `0x10010F094` | 99 |
| iOS armv7 | `0x10C8D2` | 94 |

### 1.4 parent Bezier child deformation

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x698254` | 221 |
| Android armv7 | `0x574168` | 255 |
| iOS arm64 | `0x1000F7DD8` | 210 |
| iOS armv7 | `0xF4B88` | 245 |

### 1.5 ground-correction callback worker

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6B7DF0` | 332 |
| Android armv7 | `0x585230` | 140 |
| iOS arm64 | `0x10010DFF4` | 128 |
| iOS armv7 | `0x10B8FC` | 221 |

20个函数均 fresh decompile，并完整读取disassembly，cursor全部 `done=true`。transform-order helper
已在C28用同一调用族独立闭合。phase3十个helper由C20-C28闭合；本报告只恢复root内联phase1、
phase2和它们直接的四组语义helper。

## 2. phase1：root controller、previous position 与 var tracks

updateLayers入口清assign-images producer后，phase1直接索引constructor保证存在的root；empty deque无
guard。

camera velocity X/Y/Z分别以 `!=0.0`为gate。每个live component先写root `delta.dirty=true`，再执行
`delta.pos += frameDelta*velocity`；signed zero跳过、NaN进入。之后cameraDamping只在精确等于1时
跳过，否则计算 `pow(damping, frameDelta/60)`并依次乘三轴velocity，没有正数/finite guard。

接着按physical deque order为全部node保存当前accumulated position。root的0x50 controller block随后
复制到evaluated accumulated block，只清 `root.delta.dirty`，不改其它controller值；四个root color
word重置为`0xFF808080`。matrix不在0x50 copy范围内。

variable-track helper无条件执行并live-reload deque size。每track：

- active type-zero直接跳过；
- hold或other type-zero直接取active value；
- LERP使用Player clampedEvalTime减active time；
- 非零interval以target unsigned conversion、unsigned乘法量化elapsed；
- 端点不等时可调用active easing，然后按`other*t+active*(1-t)`组合；
- 先写track current value，再调用bindParameterValue(mode=0)。

callback重入可改变容器，下一iteration重新读取size。随后type-3-root-matrix-already-propagated marker为假
时按transform-order重建root local 2x2；type3 child root保留parent前一阶段已经写入的matrix。

## 3. phase2 parent walk 与 dirty frontier

phase2从index 1按physical order处理。parent selection为unchecked topology walk：

```text
parentIndex = node.parentIndex
while nodes[parentIndex].inheritFlags & 0x00400000:
    parentIndex = nodes[parentIndex].parentIndex
parent = nodes[parentIndex]
```

没有`parentIndex>=0`、`<size`、cycle或fallback-root保护；正常root inherit bit为0终止。修改前本地添加
bounds check与非法索引回退0，本轮删除。

传给timeline evaluator的dirtyArg是camera-constraint上一frame dirty、node groundCorrection、parent
accumulated dirty、node delta dirty四项OR；evaluator内部再OR完整node flags byte。eval返回false时本node
剩余phase2全部跳过。

成功时四端只清 `node.delta.dirty=false`。flip/active/visible/position/angle/scale/slant/opacity controller
全部持久保留并在下方消费；不会中和为identity。修改前port调用`neutralizeDeltaTransformOverrides`，使
所有非根Player setter和particle child controller失效，本轮删除该helper并增加完整controller用例。

## 4. timeline evaluator

active slot done时直接返回dirty。非crossfade或other done时，clean返回false；dirty则按native顺序
复制active slot scalar、packed colors、opacity，meshType1再复制vector，随后发布type4 particle九值、
type5 FOV或type10 feedback timespan。

crossfade时node parameter pointer非null会替换currentTime。elapsed可按active ti量化：double quotient向
unsigned word转换、word乘法回绕、再转double。ratio分母不检查零。

两个change gate保留ARM condition-code语义：

- `abs(ratio)`未有序达到`1e-7`时先写ratio；dirty或old-new有序达到`1e-7`才active-copy；NaN按
  unordered结果走该分支且clean时不写payload；
- normal ratio下，clean且old-new未有序达到double epsilon时返回；unordered同样返回。

完整interpolation按flip离散选择、角度shortest wrap、scale/slant独立easing、position helper、四个
packed color channel、half-away-from-zero opacity word、mesh vector与type4/5/10 tail执行。相等端点
跳过对应easing；Variant/vector异常按已提交scalar前缀展开，不回滚。

## 5. persistent delta 与done path

active slot done时，phase2从selected parent复制0x50 accumulated block，保留copied dirty，写
active=false，再以`copiedDirty || node.flags`决定dirty；visible再AND node persistent visibleOverride，
2x2直接复制parent。其它delta controller值此路径不合成，但也不清除。

live slot先置accumulated dirty，flip与delta XOR，visible为parent.visible与visibleOverride，active再
AND activeOverride。scale相乘、slant/position/angle相加。opacity的三次可能合成都使用32-bit word
语义：先unsigned回绕乘法，再unsigned除255，结果word按int32解释；本地原signed乘除对负值/溢出
不等价且有UB，本轮新增`multiplyOpacityWordsDivide255_guess`用于delta、parent与root合成。

## 6. Bezier、world position 与 ground callback

parent meshType非零才尝试deformation；worker自身要求meshType1、sync bit0、parent active、source valid
和非空4x4 patch。active slot origin加source origin后先缩窄float并归一化u/v，patch result再按
source width/height反投影。parent coordinateMode非零选child Z，否则Y。

meshFlags bit1与child inherit angle bit共同启用angle，meshFlags bit2与child scale bits启用scale；
四个`±0.0001f`采样共用。angle由两个atan2f求和，scale用两三角形面积的固定加法分组和
`sqrt(area)/0.0002`，NaN、空维度、退化patch不防御。

之后以parent coordinateMode非零选择X/Z world平面，否则X/Y；第三轴只加parent translation。

groundCorrection为真时，从canonical root Player取raw current-dispatch。null直接返回；否则新建
current position与parent position两个TJS Arrays，各依X/Y/Z顺序append Real，retain callback receiver，
调用`onGroundCorrection(current,parent)`。result严格转object，numeric 0/1/2各先probe再read，missing
独立回退0；node XYZ依次提交，所以Y/Z异常保留早先坐标。argument/result/receiver owner顺序四端一致。

## 7. opacity、inherit flags 与matrix

ground之后，inherit bit0x400选择parent opacity；未设置且independentLayerInherit=false选择synthetic
root opacity；independent true则不做第二次opacity合成。

transform bits为`0x004..0x100`：全部七bit设置时按local transform-order重建matrix，再乘selected
parent matrix，同时合并全部flip/angle/scale/slant。部分bit路径先只合并选中parent scalar：

- independent true：直接由当前scalar重建local matrix；
- independent false：暂时移除选中的root scalar影响，重建local matrix，再恢复scalar，并用root
  matrix左乘local matrix。

所有scale除法、matrix乘加、unknown transform op与invalid flags保持raw边界。

## 8. all-node delta-position boundary

phase2完整返回后，updateLayers root在cameraConstraintDirty清零和第一个phase3 helper之前，对全部node
（包括synthetic root）执行：

```text
if queuing:
    deltaPos = (0,0,0)
else:
    deltaPos = accumulatedPos - phase1SavedPreviousPos
```

它不属于vertex helper。修改前port把pass放在vertex helper尾部、从index1开始，导致root缺失且
camera-constraint本frame位移错误进入delta；本轮移回dispatcher。若phase2/evaluator/ground抛异常，
整个delta pass不执行；pass完成后phase3异常则已发布值保留。

## 9. 本轮实施与验证限制

已实施四项语义修复：

- persistent delta只清dirty，不清controller值；
- parent passthrough walk恢复unchecked deque边界；
- opacity合成恢复uint32 wrap+UDIV 255；
- all-node delta-position恢复phase2后、phase3前的root内联位置并包括root。

新增/更新测试覆盖完整nonroot delta合成与持久性、opacity异常word、root delta-position与queued zero，
并与C27 particle child delta测试形成生命周期闭环。四库已统一命名evaluator、var-track、deform、
ground worker，给root/helper追加注释、bookmark并保存；C15/C20过时边界报告同步修正。

已执行coverage严格12列、duplicate-ID检查和`git diff --check`。当前环境缺少正式
CMake/Ninja/Emscripten依赖工具链，不能声称unit/Web build通过。
