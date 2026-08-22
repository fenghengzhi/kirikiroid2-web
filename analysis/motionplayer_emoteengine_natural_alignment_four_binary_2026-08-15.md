# MotionPlayer `EmoteEngine` trigger-byte / double 自然对齐四参考审计（2026-08-15）

## 1. 结论

`cpp/plugins/motionplayer/EmoteEngine.h` 旧声明：

```cpp
uint8_t _pad_1164_1167[1168 - 1164] = {};
```

不是原始 motionplayer 的源成员，而是把 Android arm64 的编译器对齐空隙手工写回
portable C++ 后产生的伪字段。四份当前参考给出的共同源结构是：

```text
eight adjacent trigger/state bytes
<ABI natural alignment for double>
five adjacent doubles
```

Android arm64、Android armv7、iOS arm64 在 byte cluster 后各有 4 字节自然空隙；
iOS armv7 的 `double` 对齐要求为 4，下一 double 紧接最后一个 byte cluster dword，
完全没有空隙。若原始源码真的声明了四字节数组，iOS armv7 的第一 double 必须后移，
与构造器和所有访问器均矛盾。

显式数组也不等价于“帮助 Web 对齐”：它会先消耗四个真实成员字节，再由 Web ABI
按需要继续执行自然对齐，并且 `{}` 会把 native 未初始化 padding 变成可写零字段。
本轮删除该数组，只让编译器按目标 ABI 对随后 double 自然对齐。

## 2. 四端布局边界

| 目标 | Engine ctor | trigger bytes | 最后 byte | first double | gap |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x67B76C` | `+1156..+1163` | `+1163` | `+1168` | 4B |
| Android armv7 | `0x560948` | `+588..+595` | `+595` | `+600` | 4B |
| iOS arm64 | `0x1001B7FB0` | `+788..+795` | `+795` | `+800` | 4B |
| iOS armv7 | `0x1B7788` | `+404..+411` | `+411` | `+412` | 0B |

byte cluster 的源顺序在四端一致：

```text
mirrorRequested
mirrorBase
mirrorChanged
directEdit
selectorEnabled
queuing
dirty
debugPrint
```

随后五个 double 依次为 metadata scale、inverse combined scale、hair scale、
parts scale、bust scale。物理 offset 随 STL header、pointer width 和 target ABI 改变，
但 declaration order 不变。

## 3. fresh constructor 证据

### 3.1 Android arm64

`0x67BB38` 对 selector 起始位置写一个 dword，形成
`selector=1, queuing=0, dirty=0, debugPrint=0`。`0x67BB3C` 从第一 double 开始写
两个 `1.0`，`0x67BB40` 从 hair double 开始再写两个 `1.0`；调度更早的
`0x67BB30` 写 bust double。没有指令以 `+1164..+1167` 为字段地址，也没有读取该区间。

### 3.2 Android armv7

`0x560B74` 从 raw wind owner 开始清 0x1c 字节，到 directEdit byte 为止；
`0x560B84..0x560B94` 初始化五个 double，`0x560B9A` 写四个 trigger bytes。
Hex-Rays 按数据依赖重排后的伪代码顺序不等于声明顺序，但实际地址仍显示 double 从
`+600` 开始，`+596..+599` 没有源字段访问。

### 3.3 iOS arm64

`0x1001B812C` 写 selector/queuing，`0x1001B8134` 写 debugPrint；
`0x1001B813C..0x1001B814C` 连续写五个 `1.0` double。byte cluster 结束于 `+795`，
double 从 `+800` 开始；`+796..+799` 没有独立初始化、reader 或 writer。

### 3.4 iOS armv7：反证显式数组的关键端

`0x1B796E` 写 selector，`0x1B7976` 写 queuing，`0x1B797E` 写 debugPrint；dirty 在
ctor body 的 controller seed 前由 `0x1B7A32` 等位置写一。第一 double 的低/高 word
紧接着写到 `+412/+416`（`0x1B7988/0x1B7984`），后续四个 double 也以 8 字节 stride
连续。`+412` 就是 `+411` 后满足 armv7 4-byte double alignment 的第一个位置。

因此源级 `uint8_t[4]` 不可能位于 trigger bytes 与 double 之间：即使编译器不再追加
padding，这个数组本身也会占据 `+412..+415`，把第一 double 至少推到 `+416`。

## 4. padding 的初始化与可观察边界

前三个目标中的四个对齐字节没有语义 reader/writer。构造器可能因更宽的邻接 memset/
store 偶然覆盖某些 padding，也可能完全不写；源程序不能读取它们，值不构成对象状态。
旧本地字段却有 member initializer `{}`，会把四字节变成确定为零、可寻址、参与
`sizeof` 和后续字段布局的正式成员。这改变了：

- portable Engine 的 member offset 与 object size；
- 构造 store 集合；
- 复制/调试内存观察时的确定性；
- 在 4-byte-double-alignment ABI 上根本不存在的成员区域。

删除显式数组恢复的是源结构与 ABI 边界，不要求 Web 的绝对 object layout 等于任一
native STL ABI。Web 由自身编译器插入必要的 anonymous padding。

## 5. 本地结果

`EmoteEngine.h` 现在让 `_meshDivisionRatio`（本地名，实际为 metadata scale）直接跟在
trigger byte 声明之后。注释明确：三端出现的 4B gap 是编译器自然 double alignment，
iOS armv7 无 gap，所以不存在显式 source-level padding member。

本轮不改五个 double 的 owner、顺序、默认值或行为，也不迁移其它仍带 Android arm64
trace suffix 的容器字段；那些需要各自的四参考垂直审计。
