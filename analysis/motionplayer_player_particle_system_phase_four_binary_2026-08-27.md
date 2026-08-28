# Player particle-system phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 四端函数族

### 1.1 phase root

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BC4BC` | 1290 |
| Android armv7 | `0x588A48` | 1234 |
| iOS arm64 | `0x100111D08` | 1112 |
| iOS armv7 | `0x10F51C` | 1452 |

### 1.2 两遍 child worker

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BEB84` | 220 |
| Android armv7 | `0x58AB50` | 167 |
| iOS arm64 | `0x1001140C8` | 163 |
| iOS armv7 | `0x111AF8` | 228 |

### 1.3 严格 Array element -> Player bridge

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BEA58` | 63 |
| Android armv7 | `0x58AAB0` | 47 |
| iOS arm64 | `0x100113FE4` | 37 |
| iOS armv7 | `0x1119DC` | 70 |

十二个函数均 fresh decompile，并完整读取 disassembly，cursor 全部 `done=true`。root 是 phase3
particle-emitter 后的下一阶段；四端每个 root 都只有 updateLayers 一个直接调用者。worker 与
element bridge由 root/worker内部使用。

## 2. node 入口快照与 Array owner

preview 为真时 phase root 立即返回。普通路径按 physical deque order 遍历非根 type-4 node。
每个 node 在任何 script callback 前先快照 active-slot selector；随后把 node-owned particle Array
Variant转成对象并 AddRef，整个 node pass一直使用同一 receiver。count、numeric getter、add、erase
或 child callback重入改写 activeSlotIndex/particleArrayVar 都不会切换本轮 slot/receiver。

Array `count` 是 script返回后转 signed int，不是本地 vector size。负 count让所有 `count>=1`
循环跳过。element bridge对每个 index执行 numeric PropGet，随后严格 Object转换和 required Player
native-instance查询；没有 index、null、Variant type或 adaptor type恢复。临时element Variant在返回
raw Player pointer前析构。

## 3. 已存在 child 的变换继承

这条链发生在 particle node active/done/emission gate之前，因此 inactive或completed emitter仍可改变
已有 child。

只有 `particleInheritVelocity==2` 进入位置继承。默认走 translation-only，把 parent node
`deltaPosX/Y/Z`直接加到每个 child synthetic root的 `delta.posX/Y/Z`。这不是 child的
`accumulated` evaluated block，也不额外置 dirty。

active slot未 done、`particleInheritAngle`为真且 accumulated 2x2 matrix相对四个prev快照发生变化时，
走完整矩阵分支：

1. 先保存old matrix；
2. 把四个current matrix值提交到prev；
3. 计算current angle - prevParticleAngle，按parent flip parity变号；
4. 提交prevParticleAngle；
5. 之后才检查signed child count和读取elements。

因此空/负count也提交快照，element转换异常同样保留提交前缀。determinant为零/NaN没有保护；四个
transform系数保持原版 subtraction-pair `inverse(old)*current` 运算顺序。

每个 child先对自己的root controller angle加delta、循环归一化，再走setAngleDeg；directEdit更新
emoteAngle并init，普通模式更新 `root.delta.angle`且只在改变时置dirty。position/velocity只认：

- coordinateMode 1：在X/Z平面变换root delta position和camera velocity，Y只加translation；
- coordinateMode 0：保留原版异常轴写法，先把root delta X写成旧Z+deltaZ，再把二维结果写Y/Z；
- 其它值：角度仍更新，但position和velocity完全不改。

原本地代码错误地把这些读写放在child `accumulated`，并把unknown mode当mode 0；本轮四端逐行
对照后恢复为delta controller和两个精确分支。

## 4. emission gate 与 emitCount

已有child继承完成后才判断发射：

- accumulated inactive：清persistent emitter-active byte，进入worker；
- active slot done：保留该byte，进入worker；
- frequency mode且 `particleInterp[0] == 0.0`：在active-byte写入前进入worker；
- 其它路径先快照旧active byte，再把active置true。

frequency mode使用 `60/fmin` 与 `60/f`。首次active从frequency区间随机初始化timer；每frame减
speed-scaled `_deltaTime`，然后在 `timer<=0` 时循环随机下一周期、累加并增加emitCount。只有端点
不等才消耗RNG。fmin正数时最后把timer压到 `min(timer,60/fmin)`；零、负数、NaN、Infinity和不能
推进timer的周期均保留原版除零/无限循环边界。

count mode只在完整node flags byte非零时调用一次random，即使两个count端点相等；结果以目标
signed int toward-zero conversion得到emitCount，不clamp。其它trigger不创建child但仍保留前述
active-byte发布。

## 5. source选择与child构造生命周期

emitCount正数时独立retain node的particle motion-source Array receiver，跨count、numeric getter、
全部spawn临时值和可能的child worker保持owner。source count为0时存在四端共同死循环：先把正
emitCount减到非正、调用worker，然后回到同一减法/worker块，永不退出。

非零count时选择：

```text
sourceIndex = int_toward_zero(random() * signedSourceCount)
selected = sourceArray[sourceIndex]
parts = split(selected.AsString(), "/")
chara = parts[1]
motion = parts[2]
```

没有index clamp或parts size gate；piece 0被忽略，empty pieces保留。随后 `new Player(RM)`，在
adaptor创建前写canonical root Player和immediate parent Player raw links。null non-throwing adaptor
结果不会delete native child，也不停止初始化；Array之后会收到Void childVar，native child泄漏。

成功或失败边界下的发布顺序为：packed color weight、findMotion context Variant CopyRef、zFactor、
chara、play、evaluated opacity到child root delta、随机position/velocity、flip、angle、zoom、camera
velocity、damping，最后Array `add`。

## 6. 随机分布、方向与root controller

position subtype 2为固定32边长box，RNG顺序X、Y、可选Z；subtype 1为disk或sphere-volume。3D球体
使用三个random和promoted single-precision `1.0f/3.0f` 的 `pow`，不是cbrt。非零Z再乘
`sqrt(det(accumulated matrix))`，不取abs、不检查负det。

slot ox/oy从入口快照读取，XY offset经parent accumulated 2x2变换。speed、particle angle、zoom和
direction spread都只在各自端点不等时消耗RNG，顺序固定。flyDirection 2根据child总时长和
acceleration ratio反推速度，零时长、decay非正或分母为零均直接进入IEEE/libm边界；mode 1沿采样
offset，mode 0沿matrix方向。3D方向对XY速度施加长度投影。

position、flip、angle和zoom全部发布到child synthetic root `delta` controller：

- coordinateMode 0写X/Y/Z；
- coordinateMode 1写X/Y/Z但把sampled Z放Y、transformed Y放Z；
- unknown mode不写position、速度保持初始0，但仍发布flip/angle/zoom。

本轮修复前，本地错误写了child `accumulated`，使worker被emitCount>1跳过时新controller状态落错
生命周期层。现在位置直接写delta，flip/angle/zoom调用四端保留的Player setters；新增测试同时
断言delta已发布、accumulated尚未评估。

applyZoomToVelocity mode 1乘zoom，mode 2无条件除zoom；distance-fit flyDirection 2跳过这一步。
inheritVelocity mode 1在 `_deltaTime != 0.0` 时另加parent delta/dt，负dt有效。最后把acceleration
ratio原样写child camera damping。

## 7. Array publication 与 worker调度

childVar通过Array `add`发布后重新读取signed count；若 `count > particleMaxNum`，只调用一次
`erase(0)`，不循环收缩。maxNum为零会立即删掉刚加入的唯一child；负max更容易触发。

一个node pass最多构造一个child。emitCount不是spawn loop次数：`emitCount<=1`时构造后立即调用
worker；`emitCount>1`时跳过worker并continue，剩余count直接丢弃。这也是新child root delta与
accumulated在本frame可能不同的可观察边界。

无spawn或其它快捷路径统一调用worker一次。source receiver、selected ttstr、split vector和adaptor
Variant的析构顺序覆盖构造后worker；worker/Array callback异常按已完成的publication部分展开，
不回滚Array或child状态。

## 8. 两遍child worker

worker再次独立retain particle Array并读取signed count。

第一遍删除 stopped child；playing child只在deleteOutside为真时检查bounds。倒置bounds被保留；
有效bounds必须与root Player outside rectangle严格重叠，边接触、unordered/NaN比较均删除。erase
后重新读取count并把index减一，使下轮重试同一numeric slot；脚本erase不缩短Array时可以无限重复。
erase result Variant保持存活到post-erase count读取完成。

第二遍冻结第一遍最终count，不因重入刷新。每child依次：复制parent camera angle、directEdit时init、
发布clipAABB/meshAncestor/visibleAncestor三个borrowed raw links、frameProgress(parent deltaTime)、
updateLayers、把child pending events整体前插到parent队首并clear child range。mesh separator选择
`&particleNode`，否则转发既有meshAncestor；visibleAncestor允许跨Player、不AddRef。

## 9. 本地结论与验证限制

除本轮识别的root delta/unknown-coordinate差异外，active-slot snapshot、Array owners、transform
snapshots、emission、RNG顺序、source选择、child owner、worker两遍流程和异常边逐项匹配。已实施：

- existing-child angle/position/translation/velocity继承恢复为root delta controller；
- unknown coordinateMode不再误走mode 0 position/velocity；
- spawned child position、flip、angle、zoom恢复为delta/setter路径；
- 更新spawn断言并新增unknown-coordinate matrix-inheritance回归用例。

四库已统一命名worker和element bridge，给root/helper追加注释、bookmark并保存。已执行coverage严格
12列、duplicate-ID检查和 `git diff --check`。当前环境缺少正式CMake/Ninja/Emscripten依赖工具链，
不能声称unit/Web build通过。
