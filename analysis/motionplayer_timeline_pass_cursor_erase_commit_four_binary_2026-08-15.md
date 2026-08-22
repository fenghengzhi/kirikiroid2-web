# Timeline pass cursor width, erase loop and partial commits — four-reference reconstruction

Date: 2026-08-15

本纵切面 fresh 复核零参数 timeline `pass()` facade与 Engine flush核心。整体状态机与
2026-08-11文档一致；新证据闭合了 frame cursor的32位算术/符号扩展，以及各阶段
异常时不会回滚的提交顺序。

## 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| D3D pass facade | `0x530E30` | `0x495016` | `0x100233464` | `0x2321A6` |
| Engine pass core | `0x67A100` / `0x2F8` | `0x55FCC4` / `0x1B6` | `0x1001B3FE4` / `0x274` | `0x1B3BBC` / `0x1EA` |
| HM3 at | `0x689514` | `0x569DBC` | `0x1001B374C` | `0x1B32A8` |
| blend enqueue | `0x67098C` | `0x55ACDC` | `0x1001AE178` | `0x1AD918` |
| variable enqueue | `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |

四个D3D facade都只解析primary Engine并无参数直转；无dt、result、callback state或
额外guard。Engine从active-label vector的index 0开始，每轮重新读取当前begin/end，
因此erase后同一逻辑index会立即处理原下一项。

## 共同状态机

```cpp
uint32_t activeIndex = 0;
while (activeIndex < activeLabels.size()) {
    const ttstr &label = activeLabels[activeIndex];
    TimelineState &state = timelineStates.at(label);

    if (state.loopBegin >= 0.0 ||
        ((state.flags & 2) && (state.flags & 4))) {
        ++activeIndex;
        continue;
    }

    if (state.flags & 2) {
        setBlend(label, 0, 20, 0, true);
        state.flags |= 4;
    }

    for (trackIndex in state.timelineData->variableList) {
        if (!(state.flags & 2) || track.instantVariable)
            flushFramesAfterCursor(track, state.frameCursors[trackIndex]);
    }

    if (state.flags & 4)
        ++activeIndex;
    else
        activeLabels.erase(activeLabels.begin() + activeIndex);
}
```

`loopBegin`使用ordered `>= 0.0`：`-0.0`视为loop并跳过，NaN不满足而进入flush。
bit2+bit4项整项跳过；bit2未置位的普通timeline flush全部track后erase；bit2已置位
但bit4未置位时先排20-frame auto-stop fade并置bit4，只flush instant track，保留active。

## frame cursor的32位边界

Android arm64关键指令为：

```text
LDR W11, [cursorVector, trackIndex, LSL#2]
ADD W10, W11, #1
SXTW X10, W10
CMP frameCountX, X10
...
ADD W25, W11, #2
...
SXTW X10, W25
ADD W25, W25, #1
```

iOS arm64同样是 `LDR W13 -> ADD W8 -> SXTW X10`，循环变量也在W寄存器自增后
SXTW。两份armv7天然以32位执行同一环绕。因此二进制语义是：

1. cursor+1在32位域环绕；
2. 结果解释为signed int32；
3. 比较/索引前符号扩展到平台size_t宽度；
4. 循环自增继续在32位域环绕。

源码此前先把cursor转成`size_t`再加一，在arm64的 `INT32_MAX` 环绕边界不同。本轮
改为显式uint32加法→int32结果→size_t符号扩展，固定实际机器行为。直接可测边界：

- cursor `-1`：32位加一得到0，从frame 0 flush到尾；
- cursor `-2`：得到signed -1，转size_t后大于普通frame count，跳过全部frame；
- cursor `INT32_MAX`：加一环绕为`INT32_MIN`，而不是正的`0x80000000` size_t。

## owner、erase与异常提交

- HM3使用`at`。缺失active key在任何flags/track副作用前抛`std::out_of_range`；不
  插入默认state，active vector保持当前内容。
- parallel fade的controller enqueue先发生，返回后才`flags |= 4`。若setBlend/lazy
  init/setTarget抛出，bit4不提交；其内部可能已完成的lazy init不回滚。
- bit4提交后才flush instant frames。后续任何variable enqueue异常都保留fade队列与
  bit4，并保留active label。
- 普通timeline逐frame即时提交variable写；后续frame异常不回滚前项，且erase尚未发生。
- vector erase在全部目标frame处理完后执行。libstdc++/libc++各自搬移后继ttstr owner
  并释放尾项，capacity不缩；循环不递增index，从搬来的下一项继续。
- 核心无timelineData null guard、frameCursors长度guard或track/frame结构验证；依赖
  play/init建立的内部不变量。

## 本地修正与回归

`.at`、ordered loop门、parallel/instant过滤、20-frame fade、bit4重入门和
erase-without-increment原已一致。本轮只修正frameIndex宽度/环绕表达，并增加
`cursor=-1/-2`回归：前者发布从frame 0开始的最终值，后者不发布任何frame，两项普通
timeline随后都从active vector移除。

验证结果：Emscripten syntax-only测试翻译单元通过（仅既有 literal-operator弃用
warning），`Web Debug Build` 以3个增量步骤完成最终链接，目标 `git diff --check`
通过。
