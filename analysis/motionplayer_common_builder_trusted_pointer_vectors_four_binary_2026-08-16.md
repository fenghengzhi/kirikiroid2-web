# MotionPlayer common builder 的可信指针向量边界（四参考二进制，2026-08-16）

## 结论

四份参考二进制中的通用 render-command builder 对四类内部指针序列采用相同的强前置条件：主 prepared-item 向量、辅助 group 向量、group 的 paint-box union 子向量、group 的 alpha-mask 子向量都不允许以空指针表示空槽。循环从容器取出元素后立即读取对象字段，没有空指针比较或跳过分支。

因此，空元素不是可容忍的稀疏容器状态，而是已经破坏的内部状态；参考实现会在首次解引用处失败。端口此前的 `continue` 把这种损坏静默降级成“元素不存在”，改变了边界行为，也可能让后续命令队列在不完整输入上继续生成。

## 函数对应

| 平台 | 通用 render-command builder |
|---|---:|
| Android arm64 | `0x6C2208` |
| Android armv7 | `0x58C7C4` |
| iOS arm64 | `0x1001167BC` |
| iOS armv7 | `0x114118` |

## 主 prepared-item 向量

四个平台都先从迭代器槽位加载对象指针，紧接着从该指针读取 raw flag；中间没有与零比较：

| 平台 | 取指针 | 首次解引用 |
|---|---:|---:|
| Android arm64 | `0x6C319C` `LDR X26, [X23]` | `0x6C31A0` `LDRB W8, [X26,#0x13]` |
| Android armv7 | `0x58D152` `LDR R11, [R10]` | `0x58D156` `LDRB ..., [R11,#0xB]` |
| iOS arm64 | `0x1001173E8` `LDR X25, [X27]` | `0x1001173EC` `LDRB ..., [X25,#0x13]` |
| iOS armv7 | `0x114210` `LDR R3, [R6]` | `0x114212` `LDRB ..., [R3,#0xB]` |

Android armv7 的反编译也直接呈现为 `v51 = *v7; if (*BYTE(*v7 + 11))`：分支检查的是对象内 flag，而不是对象指针本身。

## 辅助 group 向量

外层 auxiliary/group 遍历同样把每个槽位视为有效对象：

| 平台 | 取 group 指针 | 随后的首次字段读取 |
|---|---:|---|
| Android arm64 | `0x6C3274` `LDR X27, [X28]` | 立即从 `[X27,#0x18]` 等位置读取 paint-box/child-vector 字段 |
| Android armv7 | `0x58D312` `LDR R11, [R10]` | 立即从 `[R11,#0x10]` 等位置读取字段 |
| iOS arm64 | `0x1001174DC` `LDR X27, [X23]` | 立即读取 float 与 child-vector 字段 |
| iOS armv7 | `0x114EA8` `LDR R2, [R1]` | `0x114EAA` 起读取 `[R2,#0x10]` 等字段 |

没有任何平台在 group 指针与零之间建立分支。空 group 会在读取 paint box 或 child-vector 时失败。

## paint-box union 子遍历

第一段 child 遍历只检查 child 对象内的 rawFlag21；对象指针本身被直接解引用：

| 平台 | 取 child 指针 | rawFlag21 读取 |
|---|---:|---:|
| Android arm64 | `0x6C328C` `LDR X10, [X8]` | `0x6C3290` `LDRB ..., [X10,#0x15]` |
| Android armv7 | `0x58D32E` `LDR R2, [R0]` | `0x58D330` `LDRB ..., [R2,#0xD]` |
| iOS arm64 | `0x1001174F4` `LDR X10, [X8]` | `0x1001174F8` `LDRB ..., [X10,#0x15]` |
| iOS armv7 | `0x114EC4` `LDR R2, [R0]` | `0x114EC6` `LDRB ..., [R2,#0xD]` |

若 flag 为真，builder 才把 child paint box 合并到 group union；若 flag 为假则跳过的是一个有效 child 的几何贡献，而不是一个空槽。

## alpha-mask 子遍历

第二段 child 遍历重复同一可信指针边界，然后检查 rawFlag21 与 leaf Variant 是否为 Void：

| 平台 | 取 child 指针 | 首次字段读取 |
|---|---:|---:|
| Android arm64 | `0x6C36B4` `LDR X26, [X21]` | `0x6C36B8` `LDRB ..., [X26,#0x15]` |
| Android armv7 | `0x58D700` `LDR R6, [R4]` | `0x58D702` `LDRB ..., [R6,#0xD]` |
| iOS arm64 | `0x100117814` `LDR X22, [X25]` | `0x100117818` `LDRB ..., [X22,#0x15]` |
| iOS armv7 | `0x115288` `LDR R6, [R2]` | `0x11528A` `LDRB ..., [R6,#0xD]` |

这里允许跳过的是 `rawFlag21 == 0` 或 leaf 为 Void 的有效 child。不存在先判断 child 指针是否为空的入口。

## 源码修正

`cpp/plugins/motionplayer/PlayerRenderExecute.cpp` 的 `Player::buildRenderCommands` 已移除四个端口自创的空指针跳过：

- 主 prepared-item 循环不再对 `entryPtr` 执行 `if (!entryPtr) continue`；
- auxiliary group 循环不再对 `grpPtr` 执行 `if (!grpPtr) continue`；
- paint-box union child 循环只检查 `childPtr->rawFlag21`；
- alpha-mask child 循环直接绑定 `*childPtr`，再检查对象内 flag 与 leaf Variant。

这不会改变合法输入的数据流；它只恢复损坏内部容器的原生失败边界。所有容器仍可为空，区别仅在于非空容器里的每一个已存储指针都必须有效。

## IDB 更新

四份 recovery IDB 的 common builder 函数头与上述四个代表性元素加载点都已追加可信指针容器注释，并保存到 `out/ida-recovery/` 对应平台目录。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过，只有既有 `_tss` literal-operator 弃用警告；
- `KRKR2_WASMTIME_HEADLESS=1` 同一翻译单元语法检查：通过，同一既有警告；
- Web Debug `motionplayer` archive：`2/2` 通过；
- Wasmtime Headless Debug `motionplayer` archive：`2/2` 通过；
- 两套 archive 只出现既有 `_tss` literal-operator 弃用警告；
- Web Debug 完整目标：`1/1` 链接通过；只出现既有 pthread/memory-growth、JSPI 与 Emscripten JS library 警告。
