# Timeline play/log/active-vector commit — four-reference reconstruction

Date: 2026-08-15

本纵切面 fresh 复核 `playTimeline`、`isTimelinePlaying`、`stopTimeline` 的 Engine
核心与 D3D facade，纠正旧移植中把 play miss 当异常的行为，并补齐 active-label
vector 的算法与提交边界。

## 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| D3D play facade | `0x530C60` / `0xC` | `0x494F08` / `0x8` | `0x100233364` / `0xC` | `0x232058` / `0x8` |
| Engine play | `0x670350` / `0x5E8` | `0x55AA70` / `0xE0` | `0x1001ADE0C` / `0x174` | `0x1AD53C` / `0x1AC` |
| D3D is-playing facade | `0x530C6C` / `0xC` | `0x494F10` / `0x8` | `0x100233370` / `0xC` | `0x232060` / `0x8` |
| Engine is-playing | `0x670938` / `0x54` | `0x55ACAC` / `0x30` | `0x1001AE100` / `0x78` | `0x1AD8CC` / `0x4C` |
| D3D stop facade | `0x530C78` / `0xC` | `0x494F18` / `0x8` | `0x10023337C` / `0xC` | `0x232068` / `0x8` |
| Engine stop | `0x679680` | `0x55F6E4` | `0x1001B341C` | `0x1B2F70` |
| ordinary one-arg log | `0xA16CA4` | `0x76483A` | `0x1002591D4` | `0x25A52E` |

D3D 三个 facade 都只解析其 primary Emote object 的 Engine 指针并 tail-forward；
不会在 facade 内再复制 label。脚本/NCB 调用方拥有按值 `ttstr` 参数，Engine 接收对
该调用期 owner 的引用。flags 以 32 位原样转发，Engine 内只在此处直接解释 bit 0，
其余位继续传给 controller 初始化。

## 诊断字符串的宽字面量证据

普通 IDA string 搜索无法命中 TJS 宽字面量，因此按 UTF-16LE bytes fresh 搜索
`timeline label not found '`。四端各只命中一份共享字面量：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x14D3AAA` | `0xD844DC` | `0x10195FEEE` | `0x1752252` |

xref 进一步证明每端恰由 Engine play 与 `getLoopTimeline` 两条业务路径使用；play
末尾再拼接 `'.`，调用同一个普通 importance=0 单参数日志 wrapper。该 wrapper 正常
返回，不是异常 helper。两个拼接临时字符串随后按正常 owner 顺序释放。

## play 的精确提交顺序

四端共同数据流为：

```cpp
void playTimeline(const ttstr &label, uint32_t flags) {
    if (flags & 1)
        stopTimeline(ttstr());                 // clear active vector first

    auto state = timelineStates.find(label);   // HM3, non-inserting
    if (state == timelineStates.end()) {
        TVPAddLog(L"timeline label not found '" + label + L"'.");
        return;                                // ordinary return, no rollback
    }

    if (std::count(active.begin(), active.end(), label) == 0)
        active.push_back(label);               // CopyRef into vector owner

    if (!state->second.timelineData)
        initializeTimelineState(state->second);
    initializeTimelineControllers(state->second, flags);
    seekTimeline(state->second, 0.0);
}
```

关键边界：

- `flags & 1` 的 clear 早于 HM3 find；未知 label 仍会先停止全部活动 timeline，之后
  记录日志并返回。
- miss 不插入 HM3，也不向 active vector 追加 label。
- active 去重是完整范围 `std::count`，四端都会比较所有现有元素；本地旧
  `std::find` 虽在纯比较结果上通常等价，但不是原始算法/调用链。
- active append 成功后，后续 lazy state 初始化、controller 初始化或 seek 若抛出，
  已追加的 label 不回滚。
- 若 label 是 null-backed empty key，HM3 仍可命中已有空键并把空 label 加入 active
  vector；后续空 label query/stop 仍采用下述“任意/全部”语义。

## is-playing 与 stop

这两个函数判断的是 `ttstr` backing pointer 是否为 null，而不是扫描 UTF-16 payload
判断长度。本地 `ttstr::IsEmpty()` 正好是同一 pointer-null 测试。

```cpp
bool isTimelinePlaying(const ttstr &label) {
    if (label.IsEmpty())
        return active.begin() != active.end();
    return std::find(active.begin(), active.end(), label) != active.end();
}

void stopTimeline(const ttstr &label) {
    if (label.IsEmpty()) {
        active.clear();                         // release elements, keep capacity
        return;
    }
    auto first = std::find(active.begin(), active.end(), label);
    if (first != active.end())
        active.erase(first);                    // first match only
}
```

Android old-libstdc++ clear 路径正向 Release 元素后把 end 设回 begin；iOS libc++
表现为从 end 反向销毁。两者都是各平台 `vector<ttstr>::clear()` 的实现差异，capacity
均保留。命名 stop 只擦除第一项，重复项之后仍可保持 active；is-playing 则首个命中
即可返回 true。

## 本地修正与回归

本轮把 Engine play 的 `TVPThrowExceptionMessage` 改为四端共同的精确
`TVPAddLog` 拼接和正常 return，并把 active 去重从 early-exit `std::find` 恢复为
full-range `std::count`。回归固定：

- flags 0 的未知 label 正常返回、HM3 不增长、既有 active label 保持；
- flags bit 0 的未知 label 先清空 active vector，再正常返回；
- 两条 miss 都不向 active vector 追加未知 label；
- 既有空查询、first-only named erase 与 clear-retains-capacity 行为继续保留。

验证结果：Emscripten syntax-only 测试翻译单元通过（仅既有 literal-operator弃用
warning），`Web Debug Build` 以3个增量步骤完成最终 `index.html` 链接，目标
`git diff --check` 通过，并且全树定向搜索已无 play “label not found” throw旧结论。
