# MotionPlayer PreparedRenderItem 布局与生命周期：四参考二进制联合复原（2026-08-13）

## 1. 结论

`PreparedRenderItem` 不是每帧临时值，而是由每个 `MotionNode` 懒创建并独占的持久
对象。prepared-item builder 重用同一个地址、逐帧覆盖内容；调用者栈上的 main/aux
列表和 item 内的 `childItems` 都只是 `PreparedRenderItem *` 的借用容器。删除节点时，
节点先析构并释放该 item、把节点槽清零，然后才逆序析构节点其余成员。

四个当前参考目标共同否定了一条旧本地布局假设：两个字符串之后先放的是平凡 flag
字节组，随后才是 `childItems` vector。不能把 `childItems` 声明在 flag 组之前。

## 2. 四目标函数映射

| 参考二进制 | 懒创建 item | 节点析构 | item 析构 |
|---|---:|---:|---:|
| Android arm64-v8a | builder 内联 `0x6C06C0` | `MotionNode_destroy_guess` `0x6F206C` | `PreparedRenderItem_destroy_guess` `0x6F21DC` |
| Android armv7 | `ensureNodePreparedRenderItem_guess` `0x58BDF0` | `MotionNode_destroy_guess` `0x5AF220` | `PreparedRenderItem_destroy_guess` `0x5AF2D0` |
| iOS arm64 | `ensureNodePreparedRenderItem_guess` `0x1001157BC` | `MotionNode_destroy_guess` `0x10012A48C` | 内联于节点析构 |
| iOS armv7 | `ensureNodePreparedRenderItem_guess` `0x113108` | `MotionNode_destroy_guess` `0x1290A6` | 内联于节点析构 |

Android arm64 的 item 懒创建被优化器内联进
`Player_appendPreparedRenderItems_guess`；其余三个目标保留独立 helper。是否具有独立
函数边界是优化差异，不是不同的对象所有权模型。

## 3. 懒创建与稳定地址

四端都先读节点的 `preparedRenderItem` 槽：

| 目标 | 节点槽 | `sizeof(PreparedRenderItem)` |
|---|---:|---:|
| Android arm64 | `node+1904` | `0x1B0` / 432 |
| Android armv7 | `node+1664` | `0x148` / 328 |
| iOS arm64 | `node+1920` | `0x1B0` / 432 |
| iOS armv7 | `node+1628` | `0x148` / 328 |

槽非空时直接返回原指针；槽为空时才分配、初始化字符串/Variant/vector 的空状态与若干
平凡字段，最后把指针写回节点。builder 因而不是 `new` 一个每帧 item，也不把 item
所有权移交给输出 vector。稳定 item 地址还是以下原生关系成立的前提：

```text
MotionNode --独占、稳定--> PreparedRenderItem
main/aux vector --------借用--------^
childItems vector ------借用--------^
parentItem -------------借用--------^
```

外层递归构造中可按需为 visible ancestor 或 stencil/mask node 调用同一个懒创建路径；
仅仅创建 item 不等于把它加入 main/aux 输出列表。

## 4. ABI 布局证据

下表只记录跨端对应关系；偏移是分析坐标，不应复制到编译源码注释中。

| 源级成员/所有者 | 64-bit 两端 | 32-bit 两端 | 证据 |
|---|---:|---:|---|
| `ownerLabel` | `+0` | `+0` | 首个字符串，最后析构 |
| `commandSrc` | `+8` | `+4` | 第二个字符串，倒数第二析构 |
| flag 字节组开头 | `+16` | `+8` | constructor/builder 直接字节写入 |
| `drawFlag` | `+19` | `+11` | builder 在 source/opacity 后写入 |
| `childItems` begin/end/cap | `+24/+32/+40` | `+16/+20/+24` | 懒创建清零；析构只释放 begin 缓冲区 |
| `blendMode` | `+48` | `+28` | active slot 写入 |
| `corners[8]` | `+136` | `+112` | clip remap 后从 node 复制 32 字节 |
| `packedColors[4]` | `+168` | `+144` | colorWeight 后再 clip-remap |
| `opacity` | `+232` | `+208` | accumulated opacity |
| `stencilComposite` | `+244` | `+220` | node stencil field |
| `commandKey` | `+248` | `+224` | 字符串析构点 |
| `sourceState` | `+256` | `+228` | 指向节点持久 source descriptor，借用 |
| `parentItem` | `+264` | `+232` | visible-ancestor item，借用 |
| 三个 Variant | `+284/+304/+324` | `+248/+260/+272` | 逆序析构 |
| 三个末尾 vector | `+344/+376/+400` | `+284/+300/+312` | 逆序释放各自缓冲区 |

最重要的声明顺序约束是：

```cpp
ownerLabel;
commandSrc;
flags...;
childItems;
// ...trivial fields...
commandKey;
// ...borrowed pointers/trivial fields...
commandVariant;
leafLayer;
composedLayer;
commandCompositeMeshPoints;
// ...trivial field...
commandBezierPatchPoints;
meshPoints;
```

`childItems` 的元素类型是裸 item 指针。四端析构只释放 vector 自己的连续缓冲区，
没有逐元素 `delete`，因此其元素严格是借用引用。

## 5. 精确析构次序

64-bit 两端的 item owning suffix 完全同偏移；32-bit 两端也完全同偏移。共同逆序为：

```text
meshPoints vector
  -> commandBezierPatchPoints vector
  -> commandCompositeMeshPoints vector
  -> composedLayer Variant
  -> leafLayer Variant
  -> commandVariant Variant
  -> commandKey string
  -> childItems pointer-vector buffer
  -> commandSrc string
  -> ownerLabel string
```

Android 两端把该序列抽成独立 item destructor；iOS 两端内联进 MotionNode destructor。
之后四端均执行 `operator delete(item)` 并把节点 owner 槽写零。只有完成这一步后，才继续
逆序析构 MotionNode 自身的 mesh vectors、Variants、字符串等成员。

这同时确定异常/引用计数时序：item 所持命令 Dictionary 的 Variant 引用在节点其他
Variant 之前释放；字符串命令键在 child pointer-vector 缓冲区之前释放，而
`commandSrc`/`ownerLabel` 最后释放。

## 6. builder 中 clip-remap 后的共同写入链

四个 builder 在 packed color clip-remap 返回后保持下列共同次序：

1. 从节点复制 32 字节四角坐标到 item；
2. 对 active-slot `src` 的引用计数先保留，再释放 item 旧 `commandSrc`，然后替换；
3. active-slot `blendMode` 写入 item；
4. accumulated opacity 写入 item；
5. 当前 persistent `SourceState *` 写入 item；
6. stencil composite 写入 item；
7. 合成并写 `drawFlag`；
8. 继续写 visible ancestor / coordinate / mesh 等字段并处理输出列表。

对应的关键调用/起点分别是：Android arm64 `0x6C0974`、Android armv7
`0x58B554`、iOS arm64 `0x100114D6C`、iOS armv7 `0x11271E`。具体寄存器安排和
`AddRef` 原语受 ABI 影响，但引用计数的“先 retain 新值、再 release 旧值、最后写槽”
顺序一致，因此自赋值/共享字符串时不会短暂释放最后一个引用。

另外，四端均先把 node corners 原样复制进 item；draw-affine 对 corners、bounds 与
mesh 的后续处理属于同一 builder 的后段。当前 Web 源码在复制表达式里直接套 affine，
数值正常时可等价，但源结构和异常/诊断阶段并非一比一。该问题留给独立的四端
draw-affine/materialization 纵切面处理，不在本轮仅凭局部窗口改写。

## 7. 本地纠正

`NativePreparedRenderItemState` 已把 `childItems` 从字符串之后移动到 flag 字节组之后，
使声明关系与四端 constructor/destructor ABI 一致。编译源码中旧
`MotionNode_destroy_guess@0x6F4C8C`、`MotionNode_copy@0x6F468C` 等来自早期
`libkrkr2.so` 的地址式注释也已改成四目标共同语义；所有精确地址只保留在本文。后续对
当前四参考的 deque range-erase 继续下钻还确认：真正的编译器生成节点复制赋值会浅拷贝
`preparedRenderItem` 裸 owner。旧本地赋值“保留目标 owner”的行为并不原生；正常旧树清理
只 erase 非根尾后缀，运行时不会进入该通用模板的搬移分支。

没有把 native 大小硬编码为本地 `static_assert`：Web 派生 item 还含明确标注的缓存与
诊断字段，且 Emscripten 的 `ttstr`、`tTJSVariant`、STL ABI 不等于 Android/iOS
libc++ ABI。这里需要复刻的是成员所有权与声明/析构关系，而不是伪造宿主字节布局。

### V233 后续纠正（2026-08-18）

本报告闭合了 owner/vector 的声明与析构关系，但当时仍把若干平凡字段按消费者用途
分组，而没有恢复完整自然声明顺序；同时给太多平凡字段保留了构造默认值。V233 对
四端 lazy constructor 与 builder 写入逐项复核后确认：

- `blendMode` 紧跟 `childItems`，随后才是两个 layer id；
- sort key 与 command coord Z 是同一个物理 double，matrix 后只另存 X/Y，再存 origin X/Y；
- paintBox、viewport、native float clipRect 连续，`dirtyRect` 不属于 native item；
- native 只保存 borrowed `parentItem`，portable `visibleAncestorIndex` 是 Web sidecar；
- constructor 只初始化三个 ttstr、四个 vector、三个 Variant tag、
  `rawFlag16/drawFlag/rawFlag20`、`stencilComposite` 与 `commandPatchDivision`；其余
  POD 保持 dormant；
- local `new PreparedRenderItem()` 需要 user-provided constructor，不能用首声明
  `= default`，否则 value-initialization 会先把整对象清零。

完整布局、publication commit 与新的 Wasm 328-byte base 自检见
`motionplayer_prepared_item_selective_ctor_native_layout_sidecar_commit_four_binary_2026-08-18.md`。

## 8. IDB 改善

四端节点析构均重命名为 `MotionNode_destroy_guess`。Android 两端独立 item 析构重命名
为 `PreparedRenderItem_destroy_guess`；iOS 两端在节点析构内联段记录同一字段序列。
Android armv7、iOS arm64、iOS armv7 的独立懒创建 helper 重命名为
`ensureNodePreparedRenderItem_guess`，Android arm64 对应内联分配点已加语义注释。
四份 IDB 均已原位保存。

## 9. 验证

- `Web Debug Build`：受头文件影响的 motionplayer 对象重新编译并成功链接
  `index.html/index.wasm`；最终复查为 `ninja: no work to do`。
- `Wasmtime Headless Debug Build`：普通 motionplayer 与
  `krkr2_wasmtime_guest_objects` 的受影响对象均重新编译并成功链接；最终复查同样无
  待执行工作。
- 首次并行调用超过工具的 55 秒输出窗口后，两个 Ninja 进程继续完成剩余编译/链接；
  目标时间戳更新后再以单线程 CMake build 检查，两套均成功，过程中没有编译诊断。
- `git diff --check` 通过；输出仅有仓库既有的 LF/CRLF 转换提示。
