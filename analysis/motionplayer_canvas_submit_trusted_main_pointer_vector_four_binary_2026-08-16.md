# MotionPlayer ordinary Canvas submit 的可信 main 指针向量（四参考二进制，2026-08-16）

## 结论

四份参考二进制的完整 ordinary Canvas submitter 都把排序后的 main render-item vector 视为紧密的 borrowed-pointer sequence。空 vector 会直接走无参数 setClip tail；非空 vector 的每个槽位则先加载 PreparedRenderItem 指针，立即读取对象内的两个 raw flag，随后读取 raw opacity。没有 item-pointer null 比较、替代对象或跳过空槽的恢复边。

本地 executeLayerRenderCommands 是为可维护性从完整 native Canvas submitter 拆出的 helper，不对应参考二进制中的独立函数。它此前在 production main loop 入口执行 “if (!itemPtr) continue”，会把损坏状态转成少提交一项，并让后续 item 与最终 setClip reset 正常继续。本轮删除该空槽容错，恢复 reference 的自然失败边界。

## 完整 Canvas 函数

| 平台 | ordinary Canvas submitter |
|---|---:|
| Android arm64 | 0x6C4820 |
| Android armv7 | 0x58E2CC |
| iOS arm64 | 0x1001186E0 |
| iOS armv7 | 0x11653C |

四端函数均由 Player.draw 的 ordinary Canvas 路线唯一调用。main vector 的对象和 item lifetime 由外层 prepare/draw stack 保有；submitter只借用指针，不为 item AddRef、复制或延长 lifetime。

## main loop 入口

| 平台 | 取 item 指针 | 第一/第二对象内 flag | raw opacity |
|---|---:|---|---:|
| Android arm64 | 0x6C6388 “LDR X23,[X27]” | 0x6C638C 读 +0x11；0x6C6394 读 +0x10 | 0x6C49A8 读 +0xE8 |
| Android armv7 | 0x58FA42 “LDR.W R10,[R9]” | 0x58FA46 读 +9；0x58FA4C 读 +8 | 0x58E440 读 +0xD0 |
| iOS arm64 | 0x100119EE4 “LDR X25,[X28]” | 0x100119EE8 读 +0x11；0x100119EF0 读 +0x10 | 0x100118848 读 +0xE8 |
| iOS armv7 | 0x116BC6 “LDR R4,[R5]” | 0x116BC8 读 +9；0x116BD0 读 +8 | 0x116BD8 读 +0xD0 |

每份代码的 pointer load 与第一个 byte-field load 都相邻；中间没有 CBZ item、CMP item,#0 或等价条件选择。64/32 位偏移差只来自结构布局，控制流一致。

## admission 与迭代顺序

恢复后的源码级顺序是：

1. 从 main vector 当前迭代器加载 item pointer；
2. 直接读取 skipFlag0 / rawFlag16；任一 native skip flag 命中则前进到下一槽；
3. 读取 raw opacity；只有数值零跳过，负数和大于 255 的值继续；
4. 计算/设置 target clip；
5. priorDraw 条件、source resolve、direct/buffered draw 与 ancestor alpha-mask；
6. 前进一个 pointer-sized 槽位并与 vector end 比较；
7. 整个循环结束后对 target 执行无参数 setClip reset。

flag/opacity gate 是有效 item 的业务过滤，不是 pointer validity 检查。null slot 会在第一个 flag load 处失败；参考实现不会前进迭代器，也不会为该次调用执行最终 setClip tail。此前已提交的 item 与 TJS side effects 不回滚。

## 源码修正

cpp/plugins/motionplayer/PlayerRenderExecute.cpp 的 production main submit loop 已：

- 删除 itemPtr 的 null/continue 分支；
- 在 loop 注释中标明 sorted main vector 的 borrowed/trusted pointer 语义；
- 保持两个 raw flag、raw opacity、clip 与 priorDraw 的既有顺序；
- 不把四端绝对地址写入 compiled-source 注释。

文件中 snapshot/trace-only 的格式化循环仍属于可选诊断 sidecar，不用来定义 native production admission。本轮只修改真正承载 Canvas submit 数据流的循环。

## IDB 更新

四份 recovery IDB 的完整 Canvas 函数头与各自主 main-loop item load 均已追加 borrowed main vector、立即 flag 解引用及无 null-slot admission 注释，并保存到 out/ida-recovery/ 对应平台目录。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过，只有既有 _tss literal-operator 弃用警告；
- KRKR2_WASMTIME_HEADLESS=1 同一翻译单元语法检查：通过，同一既有警告；
- Web Debug motionplayer archive：2/2 通过；
- Wasmtime Headless Debug motionplayer archive：2/2 通过；
- 两套 archive 只出现既有 _tss literal-operator 弃用警告；
- Web Debug 完整目标：1/1 链接通过；只出现既有 pthread/memory-growth、JSPI 与 Emscripten JS library 警告。
