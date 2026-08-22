# MotionPlayer PSB RL8/RL32 解码器四参考边界恢复（2026-08-15）

## 范围与映射

本纵切面复核 KRKR atlas 与 ObjSource texture materialization 共用的 PSB `compress == "RL"`
解码器，并重新确认上游 raw-resource getter 的 null/size 边界。证据来自当前四份
`reference/binaries/`，旧编译源码中的绝对地址仅迁移到本文。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| atlas caller | `0x6931C8` | `0x570F54` | `0x1000F4098` | `0xF0BE4` |
| RL8 | caller 内联 | caller 内联 | `0x1000F5510` | `0xF1F6A` |
| RL32 | caller 内联 | `0x571DA4` | `0x1000F5474` | `0xF1F10` |
| raw `GetResource(size&)` | `0x599AC4` | `0x4DD9D8` | `0x1000EDF78` | `0xEA1F0` |

保留独立函数的三端 RL32 和两端 RL8 均命名为 `decodePsbRL{32,8}_guess`；原始符号已剥离，
故不去掉 `_guess`。Android arm64 的两段与 Android armv7 RL8 仅增加块级注释/书签，不
伪造独立函数边界。

## 上游 resource pointer / size

四个 `GetResource` helper 先读取 PSB header 的 chunk-data base。base 为 null 时立即返回
null，完全不写调用者的 `uint32_t &size`；因此 atlas caller 中故意未初始化的 size 槽仍为
indeterminate。base 非 null 时才：

1. 从 node tag `0x19..0x1c` 解码 1/2/3/4-byte resource index；其他 tag 使用 index 0；
2. 无额外 category/range gate 地读取 `chunkLengths[index]` 并写 size；
3. 返回 `chunkData + chunkOffsets[index]` 的借用指针。

严格 raw-node 临时量在 getter 返回后立即释放，但较外层 texture/icon node 仍持有同一
PSB raw owner，所以正常 non-null chunk 指针在同步解码/复制期间有效。null chunk 后继续
读取未初始化 size 属于原版前置条件；当前源码没有补零。

## 共同 size gate

两个解码器都先把 `uint32_t sourceSize` 按 ARM W-register 位型重解释为 signed int32：

```text
signedSize < 1  -> return without reading source or destination
signedSize >= 1 -> sourceEnd = source + signedSize; execute at least one packet
```

所以 `0` 与 `0x80000000..0xffffffff` 全部跳过。它不是 64 位 `size_t` gate；源码中的
`signedW32` 显式保留该行为。

## RL8 packet machine

每轮先读一字节 marker：

```text
marker bit7 == 0:
    count = marker + 1                 // 1..128 bytes
    memcpy(dst, source + 1, count)
    source += 1 + count
    dst    += count

marker bit7 == 1:
    count = (marker & 0x7f) + 3        // 3..130 bytes
    value = source[1]
    memset(dst, value, count)
    source += 2
    dst    += count

continue while source < sourceEnd
```

Android arm64 的对应内联块为 `0x694218..0x694280`，Android armv7 为
`0x571508..0x571554`；两份 iOS helper 的 CFG 和数值完全相同。

## RL32 packet machine

marker 的 count 仍按像素计数，但每个像素四字节：

```text
marker bit7 == 0:
    count = marker + 1                 // 1..128 pixels
    memcpy(dst, source + 1, 4 * count)
    source += 1 + 4 * count
    dst    += 4 * count

marker bit7 == 1:
    count = (marker & 0x7f) + 3        // 3..130 pixels
    pixel = unaligned uint32 at source + 1
    repeat pixel count times
    source += 5
    dst    += 4 * count

continue while source < sourceEnd
```

Android arm64 在 `0x6940D4..0x6941A4` 内联，并将较长 run 向量化；Android armv7/iOS
helper 用标量 repeat loop。向量化不改变 packet/source/destination 提交顺序。便携源码用
`memcpy(&pixel, source, 4)` 表达允许未对齐的四字节读取，避免 Web C++ 未对齐 UB，同时
保持相同四字节位型。

## malformed stream 边界

`sourceEnd` 只用于 packet 完成后的 unsigned pointer `<` 判断。四端均没有：

- 检查 marker 后是否还剩 run value/RL32 pixel；
- 检查 literal payload 是否落在 sourceEnd 前；
- 检查解码后的字节/像素数是否符合 width×height；
- 接收或检查 destination capacity；
- 在 packet 超过 sourceEnd 时截断 count。

因此正 size 的最后一个不完整 packet会先发生 source over-read；任意声明过大的 packet 都可
写过 caller 分配的 destination。packet 完成后 source 已越过 end 时，循环才停止。当前源码
的 raw pointer、`memcpy`/`memset`/`fill_n` 与 post-packet condition 保留这些边界；不加入
防御性 clamp。

## 源码裁决

当前 RL8/RL32 算法、signed-size gate、packet 消耗和无 bounds-check 行为与四端一致，本轮
没有语义改写。修改仅包括：

- 将 `PlayerResource.cpp` 中五行具体产物/`sub_*` 绝对地址映射迁移到本文；
- 用平台层面的 inline/out-of-line 语义注释代替地址；
- 明确记录 null resource 不写 size，以及两个 decoder 的 malformed packet 边界。

这些内部 helper 不暴露脚本/API 入口，也不为单元测试增加人工导出；本纵切通过真实 atlas
caller 的四端反编译/反汇编交叉验证并依赖现有 fixture 构建链覆盖其编译集成。

## 验证

- `cmake --build --preset "Web Debug Build"`：通过，重新编译了
  `PlayerResource.cpp`（以及同轮修改的 `ResourceManager.cpp`），成功链接 motionplayer、Wasm
  与最终 `index.html`；输出只有项目既有的 `_tss`、imagepacker attributes、pthread memory
  growth/JSPI/internal-symbol 警告。
- `git diff --check`（限定 `PlayerResource.cpp`、`ResourceManager.cpp` 与本文）：通过；另行扫描
  三个文件，没有行尾空白。
- `PlayerResource.cpp` 本纵切范围的 `sub_*`、绝对地址、参考产物名扫描：无残留。
- `ResourceManager.cpp` 的 `LABEL_*`/反编译临时变量扫描：无残留。
- 四份 recovery IDB 均已保存；回读确认 arm64 的两个内联块仍带 signed-size/packet 边界注释，
  armv7/iOS 的 `decodePsbRL8_guess` / `decodePsbRL32_guess` 名称与 CFG 可解析，未把推测名误记为
  原始符号。
