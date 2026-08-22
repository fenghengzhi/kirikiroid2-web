# MotionPlayer timeline initialization nested ncb accessor/source identity 四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面以四份当前 `reference/binaries/` 为准，重新闭合
`EmoteEngine::initializeTimelineState_guess` 从 HM3 `rawElement` 到 decoded
`TimelineData -> Track -> Frame` 的完整 ncb accessor 树、Variant source owner、Count
快照位置和失败提交前缀。

发现并修正两类 portable 偏差：

1. 整个解析树仍使用 raw `motionPropGet*`，没有表达 native copied/direct-temporary
   `ncbPropAccessor` 的保活区间与“写值后失败 HRESULT 仍消费结果”边界；
2. portable 原来先 append Track，再读取该 variable 的 frame Count；四端 native 都是
   先构造 frameList accessor、快照 frame Count，成功后才 append Track。Count getter/
   转换抛出时的容器前缀因此曾不一致。

portable 现已按四端共同层级改写，并新增完整嵌套 owner probe 与 frame-Count-before-Track
异常 probe。

## 四端函数映射

| 参考 | initializer | 大小 |
|---|---:|---:|
| Android arm64 | `0x66D03C` | `0xBE4` |
| Android armv7 | `0x5590E8` | `0x4E6` |
| iOS arm64 | `0x1001ABD5C` | `0x5D0` |
| iOS armv7 | `0x1AB4B0` | `0x5B6` |

函数仍只有 `playTimeline` 与 `setTimelineBlendController` 两个业务 caller；前者后续还
执行 controller initialization 与 seek，后者只做 lazy state materialization。地址只
保留在本文和 recovery IDB。

## 完整 source/accessor 树

四端共同 owner 结构是：

```text
state.rawElement copied Variant
└─ stateObject accessor                         （函数级）
   ├─ loopBegin / loopEnd / lastTime typed real
   └─ variableList typed Variant temporary
      └─ variableListObject accessor            （外循环级）
         └─ indexed variable source Variant     （每 variable 保留）
            └─ second-copy variableObject       （每 variable）
               ├─ frameList typed Variant temporary
               │  └─ frameListObject accessor   （每 variable）
               │     └─ indexed rawFrame source （每 frame 保留）
               │        └─ second-copy frameObject
               │           ├─ time / type
               │           └─ content typed Variant temporary
               │              └─ contentObject
               │                 └─ value / easing
               └─ label
```

`variableList`、`frameList` 和 `content` 都是 typed `GetValue<tTJSVariant>` 返回值直接
构造 accessor；构造临时在 accessor retain dispatch 后立即析构。相反，indexed
`variable` 与 `rawFrame` 的 source Variant 会在同层 nested accessor 之外继续存活，
直到该循环项尾部。

这一区分来自四端栈槽/异常清理，而不是样式选择：direct temporary 没有后续业务
用途；indexed source 的完整 Variant owner 与 accessor 的第二份 dispatch owner 同时
存在，并按逆序展开。

## 精确共同顺序

四端可归一为：

```cpp
timelineData = new TimelineData();
state.timelineData.reset(timelineData);

ncbPropAccessor stateObject{tTJSVariant(state.rawElement)};
state.loopBegin = stateObject.GetValue(L"loopBegin", Tag<real>(), 0, hint);
state.loopEnd   = stateObject.GetValue(L"loopEnd",   Tag<real>(), 0, hint);
state.lastTime = stateObject.GetValue(L"lastTime", Tag<real>(), 0, hint);
state.blendWeight = 1.0f;
state.autoStop = 0.0;

state.blendController.reset(new EmoteVarController(1));
resetTarget(state.blendController, &state.blendWeight, 0, 0, false);

ncbPropAccessor variableListObject{
    stateObject.GetValue(L"variableList", Tag<Variant>(), 0, hint)};
const int variableCount = variableListObject.GetArrayCount();
double maxFrameTime = 0.0;

for (int vi = 0; vi < variableCount; ++vi) {
    const Variant variable = variableListObject.GetValue(vi, Tag<Variant>());
    ncbPropAccessor variableObject{Variant(variable)};
    ncbPropAccessor frameListObject{
        variableObject.GetValue(L"frameList", Tag<Variant>(), 0, hint)};
    const int frameCount = frameListObject.GetArrayCount();

    timelineData->variableList.emplace_back();
    Track &track = timelineData->variableList.back();
    track.label = variableObject.GetValue(L"label", Tag<ttstr>(), 0, hint);
    track.instantVariable = instantSet.contains(track.label);

    for (int fi = 0; fi < frameCount; ++fi) {
        const Variant rawFrame = frameListObject.GetValue(fi, Tag<Variant>());
        track.frameList.emplace_back();
        Frame &frame = track.frameList.back();
        ncbPropAccessor frameObject{Variant(rawFrame)};
        frame.time = frameObject.GetValue(L"time", Tag<real>(), 0, hint);
        const int type = frameObject.GetValue(L"type", Tag<int>(), 0, hint);
        if (frame.time > maxFrameTime) maxFrameTime = frame.time;
        frame.typeZero = type == 0;
        if (type != 0) {
            ncbPropAccessor contentObject{
                frameObject.GetValue(L"content", Tag<Variant>(), 0, hint)};
            frame.value = float(contentObject.GetValue(
                L"value", Tag<real>(), 0, hint));
            const double easing = contentObject.GetValue(
                L"easing", Tag<real>(), 0, hint);
            frame.easingWeight = easing == 0 ? 1
                : easing > 0 ? easing + 1 : 1 / (1 - easing);
        }
    }
}
if (state.lastTime < 0) state.lastTime = maxFrameTime;
```

## owner replacement 与渐进提交

函数入口先完整构造新 TimelineData，再把新指针安装进 `state.timelineData`，随后析构/
释放旧 owner。这个 commit 发生在 state raw metadata 的第一次读取之前。后续任意异常
都不会恢复旧 TimelineData。

三次 root scalar read 后提交 `blendWeight=1`、`autoStop=0`。新
`EmoteVarController(1)` 也遵循 new-expression-first：构造成功才替换 blend owner，旧
controller 随后析构；再以 blendWeight 地址执行 duration=0、power=0、append=false 的
target reset。

函数不写 `flags`、`currentTime` 或 `frameCursors`。这些字段只有 caller 后续阶段才可能
改变。

## Count 与 append 边界

两级 Count 都只读取一次，底层为 flags=0、name=`count`、hint=null、
receiver=objthis。HRESULT 不作 gate，写出的整数照常转换。

关键提交顺序是：

```text
variable source
 -> variable accessor
 -> frameList accessor
 -> frame Count
 -> Track append/default construction
 -> label
 -> HM4 membership snapshot

rawFrame source
 -> Frame append/default construction
 -> frame accessor
 -> time
 -> type
 -> optional content/value/easing
```

因此：

- frame Count getter直接抛出或整数转换抛出时，本 variable 尚未产生 Track；此前 variable
  的 Track 前缀保留。
- label read/转换抛出时，默认 Track 已经存在，但 label、instant flag 仍为默认状态。
- rawFrame indexed read抛出时，不产生 Frame；Frame append allocation失败时 rawFrame
  source 按 unwind 释放。
- Frame append成功后，time/type/content/value/easing 任一转换抛出都会留下默认或部分
  填写 Frame；无 rollback。
- `lastTime < 0` fallback 只在两个循环正常结束后执行。

portable 原先把 frame Count 放在 Track append 后。V143 将 Count 移到 append 前，恢复
上述异常前缀；这是行为改动，不只是注释整理。

## HRESULT 与 typed conversion

本树的 Count、indexed Variant、named real/int/string/Variant 全走 ncb typed helper：

1. 初始化临时 Variant；
2. 以 flags=0、相应 hint、receiver=objthis 调用 PropGet/PropGetByNum；
3. 不检查 HRESULT；
4. 从临时 Variant 转换/copy 出目标；
5. 析构临时 Variant。

所以 getter 写出 usable value 后返回负 HRESULT，整个解析仍继续。只有 getter 自己抛出
异常、未写值导致后续转换边界，或转换本身抛出时才中止。本轮 probe 让全部 11 次
named read 都“写值后返回 `TJS_E_FAIL`”，仍成功解出完整 Track/Frame。

## helper codegen 差异

三个非 A64 目标保留共享模板 helper，A64 对若干 Variant/string path 做了内联：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| named real | `0x65FA48` | `0x4C779C` | `0x1000F1760` | `0xEDA64` |
| named Variant | 部分内联 | `0x55218C` | `0x1000F1860` | `0xEDBF0` |
| indexed Variant | 内联 | `0x5334E0` | `0x1000691F8` | `0xED9A8` |
| named integer | `0x6609BC` | `0x496C5C` | `0x1000F17E4` | `0xEDB2C` |
| named string | PropGet内联+共享转换叶子 | `0x492100` | `0x1000F18DC` | `0xEDCB0` |

A64 的共享 Variant→`ttstr` 叶子仍是 V141 已确认的 24-caller 通用函数，不能按
Timeline 命名。模板/内联边界不改变上述 source-level owner 树。

## HM4 snapshot 与数值边界

Track append 后先提交 label，再以 HM4 instant-variable set 查找一次并存入
`instantVariable`。这是构建时 snapshot；以后修改 HM4 不会更新既有 Track。

Frame 的 max time 只使用 ordered `frame.time > maxFrameTime`。NaN 不更新 max；负值也
不会把从 0 开始的 max 拉低。metadata `lastTime` 只有 ordered `< 0` 才 fallback，NaN
保留自身。value 先从 TJS real 转 double，再窄化 float；easing 精确映射为：

- `0.0`（含 `-0.0` 比较相等）→ `1.0`；
- 正数 → `easing + 1.0`；
- 负数 → `1.0 / (1.0 - easing)`；
- NaN 因两个 ordered 比较均 false，走最后一支并传播 NaN。

## portable probes

`timeline initialization retains its nested ncb source hierarchy` 构造 root、variable、
frame、content 四层自定义 dispatch，所有 named getter 均写出值后返回失败。它验证：

- exact named read order：root 4、variable 2、frame 3、content 2；
- flags 全为0、hint均非空、receiver=objthis；
- root 首次读取可重入清除 `state.rawElement`，state accessor 仍保活整棵树；
- 成功得到 loop fields、negative-lastTime fallback、instant Track、action Frame、float
  value 和 easing weight；
- decoded TimelineData 不保存脚本 dispatch，函数返回后四层 probe 各恰好析构一次。

`timeline frame Count failure precedes native Track append` 让 frameList 的 Count getter
直接抛出 `std::runtime_error`，验证新 TimelineData、blend controller 和 root scalar
字段已提交，但 `timelineData->variableList` 仍为空。

## IDB 落地

四个 recovery IDB 均追加 V143 function comment、22 个逐地址 owner/commit 注释和
bookmark，force-recompile/decompile 后 readback：

| 参考 | function comment | address comments |
|---|---:|---:|
| Android arm64 | 1 | 22/22 |
| Android armv7 | 1 | 22/22 |
| iOS arm64 | 1 | 22/22 |
| iOS armv7 | 1 | 22/22 |

A64/iOS32 通过 function-analysis comment map 读取，A32/iOS64 通过 disasm 行注释读取；
四个 IDB 均在 readback 后原位保存。

## 验证

- 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 test TU `-fsyntax-only` 通过；仅有既有
  `_tss` warning。
- Web Debug 完整增量构建 `3/3`，最终 wasm 链接通过。
- Wasmtime Headless Debug 完整增量构建 `4/4`，guest wasm 链接通过。
- Web 与 Wasmtime 两份 wasm 均由 Node `WebAssembly.Module` 成功解析。
- 定向实现范围：旧 `detail::motionPropGet*` 为 0；6 个 accessor、2 次 Count、13 次
  typed `GetValue`；frame Count 静态位置早于 Track append，rawFrame read早于 Frame
  append。
- 限定 `git diff --check` 通过；仅有既有 LF/CRLF 转换提示。

构建闭合不代替二进制证据；嵌套 source identity、Count/append 位置与展开顺序来自本轮
四端 fresh decompile/disasm，并由两个 probe 锁定。
