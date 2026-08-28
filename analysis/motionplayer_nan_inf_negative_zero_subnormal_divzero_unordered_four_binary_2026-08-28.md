# MotionPlayer NaN / ±Inf / -0 / subnormal / 除零 / unordered compare 四参考横向审计

日期：2026-08-28  
原始任务：`MP-B03`

## 1. 结论

四个参考二进制的浮点边界不是一个“统一IEEE模式”，而是每个call site的操作序列、精度阶段、
operand order和condition-code共同定义。横向审计确认：

- ordered `< <= > >=`遇NaN均false，但`!=`遇NaN为true；
- negated/complemented compare可让unordered进入true分支，Eye/Eyebrow overshoot因此不对称；
- `std::min/max`、`fmin/fmax`、手写ternary和ARM `FCSEL`不是可互换写法；NaN与signed-zero的operand
  选择可观察；
- 多条链明确分成double计算、float窄化、float累计、double投影和再次float publication；不能提升成
  全double，也不能提前float；
- zero divisor、NaN/infinity、negative phase和pow/domain结果大多直接传播，不统一finite-check；
- angle normalization对±Inf可永不终止；某些span<=accum loop对zero/negative span也可不终止；
- subnormal没有全局flush/sanitize层；identity等判断有意读取窄化前double，故“double非零、float字段
  已下溢为0”仍可改变flag；
- AArch64 `FMAX`与ARMv7 compare/select在极端NaN payload/signed-zero上已有明确machine-level
  platform boundary，不能伪装成四端完全bit-exact。

本地实现已经按call site保留这些差异。没有发现需要修改production语义的静态差异。

## 2. 本轮 fresh 四端证据

本轮使用原生`mcp__idalib__*`对64个独立函数范围重新执行decompile、完整disassembly和
`xrefs_to`。所有disassembly均为`truncated=false`，所有decompile无error。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | IDB 更新 |
|---|---:|---:|---:|---|
| Android arm64 | 16 | 7,777 | 59 | 16条任务注释、1个书签 |
| Android armv7 | 16 | 5,752 | 43 | 16条任务注释、1个书签 |
| iOS arm64 | 16 | 4,817 | 46 | 16条任务注释、1个书签 |
| iOS armv7 | 16 | 6,470 | 44 | 16条任务注释、1个书签 |
| 合计 | 64 | 24,816 | 192 | 64条注释、4个书签；四库原位保存 |

## 3. 16类浮点archetype四端根

| archetype | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Engine slice/min/fmax | chunk `0x67A3F8` / range309 | `0x55FEF0`，95 | `0x1001B4304`，89 | `0x1B3E10`，104 |
| Var step/pow | `0x663FD8`，270 | `0x554014`，118 | `0x1001A48C0`，107 | `0x1A3E48`，114 |
| Angle step/wrap | `0x663A14`，127 | `0x553B98`，122 | `0x1001A43C0`，123 | `0x1A3838`，137 |
| Angle target gate | `0x663870`，105 | `0x553AD4`，62 | `0x1001A4308`，46 | `0x1A3798`，51 |
| Blink overshoot/remap | `0x660FBC`，250 | `0x552472`，245 | `0x1001A27A0`，223 | `0x1A19D8`，262 |
| simple spring solver | `0x65FB48`，128 | `0x551910`，139 | `0x1001A1A8C`，127 | `0x1A0BE0`，159 |
| Player frameProgress | `0x6BE44C`，278 | `0x58A63A`，240 | `0x100113B50`，197 | `0x111556`，238 |
| parameter explicit/missing | `0x6AEAF8`，230 | `0x57FA14`，152 | `0x100106D00`，116 | `0x104168`，217 |
| camera constraint | `0x6B93E0`，314 | `0x586228`，370 | `0x10010F22C`，270 | `0x10CA04`，319 |
| vertex/mesh compute | `0x6B98D0`，1,265 | `0x5866F8`，1,108 | `0x10010F6AC`，961 | `0x10CE30`，1,297 |
| calcBounds | `0x6C10E4`，480 | `0x58BE38`，402 | `0x100115C68`，332 | `0x11354C`，433 |
| camera/stereo projection | `0x6D2644`，253 | `0x596EB0`，327 | `0x100123038`，228 | `0x1220F0`，335 |
| render clip/union | `0x6C2208`，1,766 | `0x58C7C4`，1,348 | `0x1001167BC`，1,083 | `0x114118`，1,582 |
| reverse affine determinant | `0x6D8544`，142 | `0x59A400`，110 | `0x100128038`，101 | `0x1273E4`，147 |
| mesh repeat/grid/cells | `0x69AFE4`，1,829 | `0x575800`，871 | `0x1000F974C`，787 | `0xF685C`，1,035 |
| draw-affine subnormal flag | shared tail `0x6D22F4` / range31 | `0x596C40`，43 | `0x100122D54`，27 | `0x121D90`，40 |

## 4. 比较语义分类

### 4.1 ordered compare

普通C++ `< <= > >=`对应ARM ordered condition。任一operand NaN时false。代表行为：

- Var/Angle/Mouth的`phase >= 1`为false，state不complete，pow/interpolation继续传播NaN；
- camera minimum/maximum候选含NaN时不更新；
- `loopBegin < 0`对NaN和`-0.0`都false；
- viewport validity `right >= left && bottom >= top`含NaN时按具体negation/branch结构决定是否拒绝；
- parameter range validity的任一unordered compare进入invalid分支，不发布range keys。

### 4.2 inequality

`x != 0`在NaN时true。camera constraint direct offset可以是NaN；最终三轴`!=0` gate因此置dirty并把
NaN加到全部non-root nodes。把gate写成`fabs(x) > epsilon`会改变NaN和subnormal。

### 4.3 complemented unordered predicate

Eye overshoot不是对称比较：negative/NaN direction路径使用`!(target < next)`，NaN target可被判
overshoot；positive路径的ordered`target <= next`对同一NaN为false。Eyebrow又使用另一套condition
组合。不能抽取一个共享`hasPassedTarget`。

### 4.4 exact equality

- reverseAffine只拒绝`det == +0/-0`；NaN det不相等，继续除法并发布NaN；
- stereo projection以`itemZ == cameraZ`跳过；`+0 == -0`，相等infinity也跳过；NaN进入projection；
- duplicate/equal float key在stable sort比较中双方`<`均false，保留输入顺序；NaN则破坏严格弱序
  前置条件，reference不修复。

## 5. `min/max`不是同一个family

### 5.1 `std::min` operand order

Engine controller slice使用`std::min(remaining, 1.1)`，remaining是第一operand。典型实现语义
`1.1 < remaining ? 1.1 : remaining`使remaining=NaN时保留NaN；交换参数会得到1.1。

prepared group union把child paint值放在`std::min/max`第一operand，以匹配reference在相等、NaN和
signed-zero时的选择方向。改成`fmin/fmax`会改变number/NaN语义。

### 5.2 `fmin/fmax` number semantics

timeline loop residual和Canvas/accurate clip的left/top使用`fmax`类number semantics：一个NaN和一个
数时选数值operand。right/bottom常是手写`value < bound ? value : bound`，unordered时选择bound。
左右边不能统一用同一`clamp`。

### 5.3 手写ternary与FCSEL

prepared affine bounds、group union和shape AABB有意保留`lhs < rhs ? lhs : rhs`或对应compare-select。
相等signed zero以及unordered时选择哪个operand取决于代码顺序；本地注释和tests固定这一点。

### 5.4 AArch64 `FMAX` vs ARMv7

Player cursor lower-clamp和`skipToSync`在两个AArch64 reference中出现machine `FMAX`，ARMv7使用
`VCMPE`+条件move。普通finite域相同；NaN payload和signed-zero最低层可能不同。coverage已有
`MP-B11-PLAYER-CURSOR-FP`和`MP-B11-PLAYER-SKIP-FP`两条`PLATFORM_BOUNDARY`，共同源码不应为了
假装四端bit-exact而扭曲正常逻辑。

## 6. 精度阶段与窄化

### 6.1 controller链

Engine保留double original dt；slice为double，随后缩窄一次float传controller。controller的phase、
duration、pow和output主要是float；bind时再提升double。不能用original double直接step float state。

### 6.2 geometry链

accumulated position/matrix/source origin先以double计算，quad vertex store窄化float。bounds在float
点集上比较并floor/ceil为float。PreparedRenderItem保存float corners/paintBox，但sortKey为double Z。

projection先做float camera addition，再把point float提升double执行stereo公式，最后窄回float；
paintBox以投影后float做floor/ceil。任何“全程double最后一次round”都会产生不同边缘。

### 6.3 render链

clip和viewport多为float，width/height在float相减后才提升TJS Real。D3D backend再把double
destination points转GLfloat vertex。每一级rounding都属于合同的一部分。

## 7. NaN传播与非传播矩阵

| family | NaN行为 |
|---|---|
| Var duration target | ordered `<=0` false，进入queue |
| Angle/Blink/Eyebrow/Mouth duration | `!(duration>0)` true，立即snap |
| Var/Angle/Mouth phase | completion false，pow/interpolation传播NaN |
| Eye/Eyebrow overshoot | 由family-specific complemented condition决定，可clamp或传播 |
| camera minimum/maximum | 忽略NaN候选 |
| camera direct | 接受NaN；priority胜出并传播/置dirty |
| calcBounds extrema | NaN point不更新sentinel；其他点仍可建立bounds |
| projected paintBox | NaN floor/ceil compare不更新，sentinel可残留 |
| render clip | `fmax`侧可能取数值bound，ternary侧unordered选择canvas bound；最终gate依原condition |
| reverseAffine determinant | NaN通过zero gate，输出NaN Array |
| mesh point→int | helper显式NaN→0的target conversion profile；这属于B04转换，不是普通FP传播 |
| draw-affine identity | `NaN != identity`，flag true；字段原样double/float存储 |

## 8. ±Infinity边界

- Angle target/result的iterative wrap对`+Inf`反复减常数、对`-Inf`反复加常数，永不收敛；
- child motion angle和部分particle normalization同样使用while，不用`fmod`替代；
- Var/physics arithmetic可产生/传播infinity，ordered completion取决于phase符号；
- calcBounds可发布infinitenode bounds，但上层validity gate可能拒绝；
- stereo的infinite item/camera Z按IEEE subtraction/multiply/divide产生Inf/NaN；
- mesh repeat/grid的zero texture dimension或division产生Inf/NaN，随后float→int由target conversion
  profile处理；
- source width/height、wind scale、spring denominator和timeline spans没有统一finite guard。

不终止边界是reference行为，但runtime verification必须用timeout/isolation，不能让验证任务永久挂起。

## 9. `-0.0`与signed zero

- `-0.0 == +0.0`，因此identity、zero determinant和Z-equality gate通常把两者视为相等；
- `<0`对`-0.0`false，loop marker和negative clamp不触发；
- arithmetic、floor/ceil和operand-select仍可保留negative-zero sign；
- group union/shape AABB的equal-case operand order决定最终sign；
- camera三轴全为±0时`!=0` false，不置dirty、不写nodes；
- draw affine原始translation `-0.0`不使nonIdentity为true；
- float→integer转换丢失zero sign，发生在明确TJS/rect boundary之后。

现有tests用`std::signbit`锁定clip、bounds、controller slice、public properties和projection中的符号。

## 10. subnormal与underflow

draw-affine setter先保存四个double matrix、把translation m14/m24窄化float，然后用**原始double参数**
判断identity。一个非零double subnormal或小到float下溢为0的normal double会产生：

```text
stored translation float = +0/-0
nonIdentity flag         = true
```

这一区别已由四端setter指令和underflow regression固定。camera offset、controller dt和geometry
translation在各自窄化点也可能把subnormal降为zero；不能先看窄化字段再反推flag/gate。

plugin没有设置全局FPCR/FPSCR，也没有软件flush-to-zero层。CPU/GPU环境对denormal payload、exception
flag和driver shader subnormal的最低层差异属于`MP-B11/G23`平台分类；共享C++仍必须保留源级conversion
点和normal/subnormal输入的可观察control-flow。

## 11. 除零与退化分母

| 分母 | gate/结果 |
|---|---|
| Var/Angle queued duration | normalpublic setter通常阻止nonpositive；malformed/restored queue仍可`1/0`得到Inf |
| Loop span | 无zero guard；`accum/span` Inf/NaN，while可不终止 |
| Blink remap `end-begin` | 无guard；equal endpoints产生Inf/NaN remap |
| Bezier basis division | division 0执行`0/0`，basis与tessellated point为NaN |
| mesh source grid division | division 0仍生成一项并执行0-divide |
| repeat texture dimension | width/height 0进入floor/division，后续未定义转换边界 |
| stereo `itemZ-cameraZ` | finite exact equal由gate跳过；NaN/Inf组合仍可进入除法 |
| reverseAffine determinant | exact ±0返回Void；NaN不被拒绝 |
| spring/wind/metadata scale | 按各helper直接IEEE运算，不全局sanitize |

整数division和floatdivision必须分开：某些Web helper为AArch64 `UDIV` zero profile显式返回0，属于B04/
platform conversion，不可用C++浮点IEEE结论推导。

## 12. 运算结合、FMA和libm

已审计geometry/Bezier/projection路径明确保留离散multiply/add序列，不使用FMA；Bezier basis的两个看似
相同系数也按reference括号分别计算。stereo逐项double运算后窄化。不可为“优化”重关联。

`powf`、`atanf`、`fmod`和GPU shader transcendental的最后bit依赖libm/driver；branch和publication
必须相同，跨平台bit-exact transcendental属于明确platform boundary。普通finite regression应使用
有依据的exact或tolerance，而不是把所有结果都宽松处理。

## 13. 对象/异常边界

浮点异常flag不被plugin读取，普通IEEE NaN/Inf不抛C++异常。真正的异常仍来自property/conversion/
allocation/callback。函数常在浮点结果已写入persistent field后才调用可抛操作；异常不回滚NaN/Inf
publication。

可能不终止的angle/span loop也不会进入C++cleanup；这是hang而不是throw。验证必须在子进程/
wasmtime fuel/ADB timeout下执行。

## 14. 本地实现与tests

主要实现位置：

- `cpp/plugins/motionplayer/EmoteEngine.cpp:31`、`:3817`：operand-ordered slice和timeline fmax；
- `cpp/plugins/motionplayer/EmoteVarController.cpp:29`：Var phase/pow；
- `cpp/plugins/motionplayer/EmoteAngleController.cpp:41`、`:82`：Angle wrap与duration family；
- `cpp/plugins/motionplayer/EmoteBlinkController.cpp:151`：overshoot/blink；
- `cpp/plugins/motionplayer/EmoteSpring.cpp:22`：spring solver；
- `cpp/plugins/motionplayer/PlayerVariable.cpp:66`：parameter normalization；
- `cpp/plugins/motionplayer/PlayerUpdateGeometry.cpp:8`、`:153`：camera与vertices；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:190`、`:321`：projection/bounds；
- `cpp/plugins/motionplayer/PlayerRenderExecute.cpp:240`、`:396`：union/clip；
- `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:813`：reverse affine；
- `cpp/plugins/motionplayer/MotionRenderBackend.cpp:194`、`:507`：basis/grid/cell；
- `cpp/plugins/motionplayer/PlayerDrawDispatch.cpp:8`：draw-affine subnormal/identity。

现有unit资产覆盖NaN/Inf/-0 controller family、spring zero-dt、camera candidate/direct、shape/Bezier
unordered、bounds sentinels、clip operand order、division 0/-1、projection special values、draw-affine
underflow，以及public property bit-pattern preservation。本轮无新增语义缺口，不新增重复test。

## 15. 验证状态

本轮完成24,816条完整指令、192个`xrefs_to`、64条任务注释、4个书签和四库保存。两个AArch64/
ARMv7 cursor-clamp machine差异继续保持`PLATFORM_BOUNDARY`，其余共同source profile保持
`IMPLEMENTED`。

coverage与163-ticket映射随后重生成并执行严格列数、重复ID和`git diff --check`检查。正式native
unit、Web Debug、Wasmtime/ADB special-FP differential及nontermination timeout验证归`MP-V`；静态闭合
不伪称这些运行通过。

`MP-B03`没有剩余task-local静态差异。
