# MotionPlayer Player constructor initialization / Dictionary→root order（四参考，2026-08-18）

## 结论

V258 固定了 `Player` 的 111-field declaration ledger。本轮以该账本逐项重扫四个完整
constructors，不再只看单个纵切面的局部 store，得到一份完整初始化分类：

- **33 个 nontrivial 顶层字段**由 C++ member constructor 建立，其中 3 个 ResourceManager
  Variant CopyRef 同一个输入 dispatch，其余 30 个从各自 empty/Void/default 状态开始；
- **78 个 trivial/raw 顶层字段**中，71 个有明确的 source-level 初值或 constructor store；
- 剩余 **恰好 7 个** source-level POD 槽在四端都故意不写，不得安全化为零；
- constructor body 共同顺序是 descriptor Dictionary → colors Dictionary →
  `descriptor.color = colors` → synthetic root append → default transform-order copy；
- 当前源码原先把 root append 放在 Dictionary setup 之前，改变 factory/PropSet/root allocation
  抛异常时的部分构造边界。本轮已按四参考共同顺序修正。

这是行为和对象生命周期修复，不是布局修复：`sizeof(Player)`、111-field declaration order 和
所有 native offsets 均保持 V258 结论不变。

## 1. 四端恰好七个 intentionally-uninitialized POD slots

| source role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| type-1 `emoteMotionIndex` | `+0x1F8` | `+0x148` | `+0x188` | `+0x10C` |
| `layerFrameCursor` | `+0x394` | `+0x27C` | `+0x324` | `+0x23C` |
| `layerCurTime` | `+0x398` | `+0x280` | `+0x328` | `+0x240` |
| `layerNextTime` | `+0x3A0` | `+0x288` | `+0x330` | `+0x248` |
| cached motion `lastTime` | `+0x468` | `+0x310` | `+0x3F8` | `+0x2CC` |
| motion `loopTime` | `+0x470` | `+0x318` | `+0x400` | `+0x2D4` |
| final residual dispatch | `+0x560` | `+0x3A8` | `+0x4B0` | `+0x344` |

四个 constructor 的完整 instruction range 对这些 displacement 都没有 store：

- `emoteMotionIndex` 只在成功取得 type-1 division/motion-list 后、进入 initEmoteMotion 前提交
  `-1`；
- tag cursor/time trio 由 ordinary motion initialization 在遍历 tag stream 前提交；
- `lastTime/loopTime` 由 motion-content metadata 提交，直接 getters 没有未加载 guard；
- final dispatch 没有任何 producer，Android 仅保留零-xref residual consumer，iOS dead-strip
  consumer；
- 四项都不是 compiler 遗漏的 aggregate clear：相邻成员有分散明确 store，完整 constructor
  仍精确绕过这些 offsets。

当前 `Player.h` 对这七项都不提供 declaration initializer。除此之外不存在第八个未恢复的
top-level POD 默认值。

## 2. 111 字段完整初始化矩阵

### 2.1 prefix / node / camera / bounds

| member group | constructor state |
|---|---|
| `rootPlayer` | `this` |
| `parentPlayer` / `currentDispatch` | null；currentDispatch 的 store可被调度到较晚位置 |
| node-label ordered map | empty/default-constructed |
| camera position/target/stereovision 9 doubles | exact `+0.0` |
| camera offset 2 floats | exact `+0.0f` |
| bounds minX/minY/maxX/maxY | `+DBL_MAX,+DBL_MAX,-DBL_MAX,-DBL_MAX` |
| node deque | empty/default-constructed；synthetic root 是后续 body commit |

### 2.2 HM1/HM2/parameter containers

| member group | constructor state |
|---|---|
| eval-cascade HM1 / result HM2 | empty map，按 platform STL default policy |
| selected parameter raw alias | null |
| parameter vector / ramp multimap | empty/default-constructed |

### 2.3 post-ramp frame/type-1/root state

| member group | constructor state |
|---|---|
| clamped eval / emote angle / camera angle | `0.0 / 0.0 / 0.0` |
| queuing / firstFrame / directEdit / motionCompleted | `true / false / false / false` |
| division/motion-list/motion-content/priority/root-content Variants | Void/default-constructed |
| emote motion index | **untouched** |
| root cursor/current/next | `0 / 0.0 / 0.0` |
| delta time / damping | `0.0 / 1.0` |
| noUpdate/reverse/constraint/affine/internalReady/needsAssign | `true / false / false / false / false / false` |

### 2.4 source workspace / adaptor

| member group | constructor state |
|---|---|
| find-source RM / SourceCache RM | independent CopyRef of constructor dispatch |
| descriptor / internal layer / colors / work layer | initially Void |
| raw SeparateLayerAdaptor | null |
| constructor-body commit | descriptor/colors become two Dictionaries, then descriptor.color owns colors |

### 2.5 pending / velocity / affine / outside rect

| member group | constructor state |
|---|---|
| two pending ttstr | empty/default-constructed |
| camera velocity XYZ | exact `0.0` triple |
| draw affine M11/M12/M21/M22/M14/M24 | identity `1,0,0,1,0,0` |
| particle outside rect | four exact zero floats |

### 2.6 draw region / tag / event / live strings / canonical RM

| member group | constructor state |
|---|---|
| complex draw region | default-constructed empty region |
| post-region unknown dword | zero |
| type3-root marker / D3D mode | false / false |
| pixelate division | 100 |
| layer cursor/current/next | **all untouched** |
| pending event vector | empty/default-constructed |
| chara/stealthChara/motion/stealthMotion | four empty ttstr owners |
| canonical RM | third independent CopyRef of constructor dispatch |

### 2.7 late Variants / scalar cluster / late containers / tail

| member group | constructor state |
|---|---|
| findMotion context / outline / meshline / tag-source | four Void Variants |
| preview/sync/camera/stereo/prior/inherit/wait/allplaying/hasCamera | false / process-global sync snapshot / seven false |
| FOV / zFactor / frameTick | exact `0.2 / +0.0 / +0.0` |
| lastTime / loopTime | **both untouched** |
| completion / mask / processed count | signed zero / signed zero / unsigned zero |
| packed color | `0xFF808080` |
| outside / speed / mesh division | exact `1.5 / 1.0 / 1.0` |
| HM3 / HM4 / variable deque | empty/default-constructed；Android eager buckets，iOS lazy null buckets |
| final residual dispatch | **untouched** |

该矩阵解释了 constructor 中看似“乱序”的 store：编译器把无别名的 trivial initializers
调度到 nontrivial calls 之间或 body 较后位置，但 source-level member initialization仍发生在
constructor body 之前。不能用某条机器 store 的晚位置把 field 错判为 body assignment；真正需
保持先后顺序的是可观察的 Dictionary factory/assignment/PropSet/root calls。

## 3. native constructor-body 的共同调用顺序

| 目标 | descriptor factory | colors factory | `descriptor.color` PropSet | root append | factory-return releases |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6CC3F4` | `0x6CC414` | `0x6CC460` | `0x6CC55C..0x6CC584` | `0x6CC5F4 / 0x6CC634` |
| Android armv7 | `0x593762` | `0x59377C` | `0x59379A` | `0x5938A0` | `0x5938E8 / 0x5938FE` |
| iOS arm64 | `0x10011EDAC` | `0x10011EDCC` | `0x10011EDFC` | `0x10011EEF4` | `0x10011EF5C / 0x10011EF80` |
| iOS armv7 | `0x11D752` | `0x11D774` | `0x11D7A6` | `0x11D976` | `0x11D9E4 / 0x11DA00` |

共同高层顺序：

```cpp
descriptor = new Dictionary;
sourceDescriptor = descriptor;

colors = new Dictionary;
sourceColors = colors;

sourceDescriptor.color = sourceColors;

nodes.emplace_back();
nodes.front().transformOrder = processDefaultTransformOrder;

release(colorsFactoryReturn);
release(descriptorFactoryReturn);
```

两份 factory-return raw owners在 root append期间仍保持引用；正常尾部按 colors → descriptor
顺序释放。persistent member Variants另持有引用，`descriptor.color` 又为 colors 增加一份引用。

## 4. 异常与 partial-construction 边界

这个顺序带来三条不可交换的边界：

1. descriptor/colors factory、Variant assignment 或 PropSet 失败时，node deque 仍为空；没有先
   创建又在 unwind 中销毁的 synthetic root。
2. root allocation / MotionNode construction失败时，两块 Dictionary 已经逐项发布到 persistent
   member Variants，`descriptor.color` 也已经成功；factory-return guards先释放局部 owners，随后
   C++ member unwind按声明逆序释放已构造的 Player owners。
3. root成功后只复制四个 process-default transform-order words；普通 MotionNode constructor仍以
   zero order开始，只有 synthetic root获得 class default。

当前源码原先先调用 `ensureRootNode_guess`，然后才创建 Dictionary。成功路径的最终状态相同，
但上述三个 failure frontiers不同。本轮把 root block移到 PropSet 后，并让两个
`DispatchReleaseGuard_guess` 一直活到 root设置完成，恢复四参考共同顺序和 release lifetime。

## 5. child Player 的 constructor 后覆盖

每个 Player constructor 都先建立：

```cpp
rootPlayer = this;
parentPlayer = nullptr;
currentDispatch = nullptr;
```

type-3 与 particle child producer 在 child constructor成功返回后才覆盖前两项：

```cpp
child.rootPlayer = parent.rootPlayer;
child.parentPlayer = &parent;
```

它们不写 `currentDispatch`。因此这两项属于 constructor-after-publication raw alias，不应改成
额外 constructor 参数；若 child constructor在 Dictionary/root setup中失败，父子关系尚未发布，
只有 child 自己的 `root=this,parent=null` partial state参与 unwind。

## 6. local Web executable readback

修复后从最终 ordinary Web `index.wasm` 读取 symbol table：

| symbol | Web ElementIndex |
|---|---:|
| `motion::Player::Player(...)` | `0x5450` |
| `TJSCreateDictionaryObject(...)` | `0x19953` |
| `motion::detail::ensureRootNode_guess(...)` | `0x62B3` |

constructor 反汇编中的最终 call order：

```text
0x3287E8  call 0x19953   // descriptor Dictionary
0x32885F  call 0x19953   // colors Dictionary
0x328920  call_indirect  // descriptor PropSet("color", colors)
0x328926  call 0x62B3    // ensureRootNode_guess
```

因此 source patch确实进入可执行代码，没有被优化器重新交换。两个 factory调用与 root call之间
仍保留 PropSet 的间接动态调用边界。

## 7. recovery IDB 写回

四库各写回三条注释和三枚 bookmark，共 **12 comments / 12 bookmarks / 0 renames**：

- constructor entry：七个 intentionally-uninitialized source POD offsets；
- descriptor factory：Dictionary publication → PropSet → root 的顺序；
- root append：Dictionary 已发布、guards 仍存活时的 root commit/unwind 边界。

iOS armv7 different-path 安全保存：

- pre-V259 backup：
  `out/idb-recovery/v259-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v259.i64`，
  377,632,976 bytes，SHA-256
  `E5AE32E4FD0FA101D9A4B7DBD3786C44A6344836F0F2C43AE0A4024AEBBC8350`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v259.i64`；
- `C:\IDA\idat.exe -A` candidate probe 退出 0；
- candidate 覆盖 canonical 前解析并验证两个绝对路径都位于 workspace 内；
- canonical 重新打开后在 constructor range 回读到全部 3 条 V259 comments，再关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,728,756 | `844268E21E04171A29E88C50CE8ED6A13D0781963B7034C24371A26C51FBCA93` |
| Android armv7 | 345,878,941 | `7B0AA65F895B4509B7B5B4BD2D699B8065637002ECA9C8522CA60596205F6F09` |
| iOS arm64 | 334,884,807 | `E012F75A658EFC8BC654BA9D97336E918D114EDA01469562D6B16EF1BC5D1B0C` |
| iOS armv7 | 377,632,976 | `8A6EB6E604296D3D3DEFD08FF3A543A7B03560A43937AB9449129C5140F2AF48` |

最终 IDA process/session 数为 0。

## 8. 验证与 Wasm 基线

实际完成：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 完整 `motionplayer-dll.cpp` syntax-only：均通过；
- Web Debug：3-step rebuild/link 通过；
- Wasmtime Headless Debug：4-step rebuild/link 通过；
- guest：1-step relink/exnref conversion 通过；
- 三目标随后均为 `ninja: no work to do`；
- 最终 Web constructor symbol/call-order readback通过；
- `git diff --check` 无 whitespace error，仅工作树既有 LF/CRLF warning；
- IDA process/session 数为 0。

最终产物：

| wasm | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `index.wasm` | 85,655,322 | `0x1BD31` | `0x1A4109D` | `0x5A3E40` | `0x3185F7B` | `B4FB58B358283E7C48D767AC3C6B37DDE2AE452E047808FEA132EA2FBADED514` |
| Wasmtime `index.wasm` | 85,002,463 | `0x1BA50` | `0x19E904B` | `0x5A1090` | `0x3141E11` | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` |
| guest | 151,479,107 | `0x1618E` | `0x13D7DCD` | `0x4D1630` | `0x1421EBA` | `4851CDB6B3E6C6DFF0159D2847232426685F7F7FA7651690F12537A3436CE387` |

两份主 wasm 的总 size与列出 section sizes都和 V258相同，但 hash改变，因为 CODE 内调用顺序
已变化而 section长度不变。guest 的 FUNCTION/CODE/DATA/name sizes同样不变，总文件多4 bytes；
增量来自 debug/custom metadata，不能从 section size不变误判源码修复未进入 executable。Web
constructor readback已直接证明新次序。

## 9. 本轮闭合与后续方向

V259 闭合了 top-level Player constructor state、七个 indeterminate POD和最重要的 body side-effect
顺序。仍可继续沿 constructor failure path深挖：逐个 Dictionary assignment、PropSet、root deque
growth 的 EH landing-pad cleanup顺序，尤其比较 Android Itanium EH 与 iOS armv7 SJLJ 是否存在
平台特有差异；也可以转向其他高价值对象的完整布局/constructor ledger。无论选择哪条，都不应
重新给这七个 POD增加“安全默认值”。
