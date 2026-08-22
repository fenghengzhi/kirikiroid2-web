# MotionPlayer timeline seek cursor clear、future action 与 ordered clamp 四参考复原（2026-08-15）

## 结论

`EmoteEngine::seekTimeline_guess(state, time)` 不是一个只改`currentTime`的随机访问查询，
而是带即时副作用的重建流程：先清空cursor vector但保留capacity，再扫描每条非跳过
track、逐项append cursor，最后重放扫描所得的last action；全部track正常完成后才提交
currentTime。

fresh四端指令确认两条容易被高层伪代码掩盖的边界：

- null `timelineData`没有guard，但失败发生在cursor vector已经clear之后；
- 扫描每槽时先更新`lastActionFrame`，后测试time bracket，所以target早于frame0或为NaN
  时，仍会选择并重放pre-sentinel action，而不是保持变量未设置。

另外，本地`std::max`只在signed-zero边界不等价：四端seek都用ordered
`raw <= 0 ? +0 : raw`，会把`-0`规范化为`+0`，同时传播NaN。本轮已恢复该选择。

## 四端映射与caller

| 目标 | seek helper | 大小 | pre-progress caller | play caller |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `0x66EE30` | `0x360` | `0x66ECB4` | `0x670540` |
| Android armeabi-v7a | `0x55A0F8` | `0x1CE` | `0x55A058` | `0x55AB04` |
| iOS arm64 | `0x1001AD2C0` | `0x280` | `0x1001AD1D0` | `0x1001ADF60` |
| iOS armv7 | `0x1ACA22` | `0x220` | `0x1AC928` | `0x1AD6C8` |

每端只有两条入边：play完成state/controller初始化后以time0调用；pre-progress每次loop
wrap先跑strict loop-end window，再以`loopBegin`调用seek。不存在脚本直接入口或只读
query caller。

共同原型为：

```cpp
void EmoteEngine_seekTimeline_guess(
    void *engine, void *state, double time);
```

engine用于通用setVariable、内部controller enqueue和queuing byte；state及其整个data/
controller/frame/cursor树均为借用。

## cursor vector 的clear/reuse与null-data失败前缀

四端clear锚点：

| 目标 | clear cursor end | 首次data解引用 |
|---|---:|---:|
| Android arm64 | `0x66EE70` | `0x66EE78` |
| Android armv7 | `0x55A136` | `0x55A140` |
| iOS arm64 | `0x1001AD31C` | `0x1001AD328` |
| iOS armv7 | `0x1ACA68` | `0x1ACA70` |

Android直接把vector end写回begin；iOS libc++生成了按4字节元素对齐回退end的等价
表达式。四端都不释放begin allocation、不改capacity，因此只要后续append不超过旧
capacity，storage地址保持。

data owner虽然可能在clear前被加载到寄存器，但真正读取其deque字段发生在clear之后，
且没有null test。null state的可观察前缀是：

1. 原cursor元素被逻辑删除；
2. capacity/storage保留；
3. data字段解引用失败；
4. currentTime不写。

这与window的null-data no-track + time commit明确不同。

## compact cursor与扫描算法

每条track的共同流程：

```cpp
if ((flags & 4) && track.instantVariable)
    continue; // 不append cursor

bool internal = (flags & 2) && !track.instantVariable;
cursor = 0;
lastAction = -1;
if (frames.size() >= 2) {
    scanCount = frames.size() - 1; // 不扫描tail sentinel本身
    for (cursor = 0; cursor < scanCount; ++cursor) {
        if (!frames[cursor].typeZero)
            lastAction = int32(cursor);
        if (frames[cursor].time <= time &&
            frames[cursor + 1].time > time)
            break;
    }
}
frameCursors.push_back(int32(cursor));
if (lastAction >= 0)
    replay(frames[lastAction]);
```

边界：

- flags4 instant track完全不占cursor slot，形成compact vector；window却按physical track
  index读取，错位边界继续保留。
- 空/单帧track都append 0，不dispatch。
- 两帧track只有frame0参与scan，frame1是tail sentinel；若没有bracket break，cursor最终
  append 1。
- `lastAction`在bracket test之前更新。target早于frame0时所有`current<=target`为false，
  scan仍走到末尾并重放最后一个`!typeZero` pre-sentinel frame。
- time为NaN时两个ordered compare都为false，效果同样是全扫描并重放last action；
  transition与最终currentTime继续携带NaN。
- frame time为NaN的slot不能形成bracket，但仍可在`!typeZero`时成为last action。

## cursor先提交、action后重放

四端在调用controller/setVariable之前完成当前track的cursor append。若cursor vector需要
扩容，使用平台各自的int32 vector grow/memmove路径；旧allocation只在新元素与旧前缀
复制成功后释放。

随后若`lastAction >= 0`才重放。副作用顺序为：

1. 当前track cursor已append；
2. 读取action frame与next frame；
3. 计算transition；
4. controller enqueue或普通setVariable即时提交；
5. 进入下一track；
6. 所有track完成后写currentTime。

所以较晚setter异常不会回滚已append cursor、较早track controller/HM7写入；但当前Time
仍保持seek前值。vector allocation异常则发生在当前track action重放前。

## ordered transition clamp

seek对last action计算：

```cpp
raw = frames[lastAction + 1].time - time - 1.0;
transition = raw <= 0.0 ? 0.0 : raw;
```

四端锚点为Android A64 `0x66EFD0/0x66EFF8`、Android A32
`0x55A258..0x55A26E`、iOS A64 `0x1001AD4B0/0x1001AD4C8`、iOS A32
`0x1ACBB6/0x1ACBE6`。internal与external路由各自复制一次同一ordered选择：

- raw有限负值 -> `+0`；
- raw为`-0`或`+0` -> `+0`；
- raw为NaN -> ordered `<=` false，保留NaN；
- raw正数/+Inf -> 原值。

这不同于`std::max(raw,0.0)`仅在first operand为`-0`时保留`-0`。源码现显式写
ordered ternary，避免Web编译器把生命周期无关的signed-zero边界改掉。

internal路由把transition/easing各窄化float并读取Engine queuing byte；external路由把
frame float value提升double，将transition/easing以double送入通用setVariable。

## 本地修正与回归

源码用`transitionRaw <= 0.0 ? 0.0 : transitionRaw`替换`std::max`，保持NaN传播并
规范化signed zero。

新增回归预留8个cursor capacity并放入两个旧值，再对一条两帧track执行`seek(0)`：

- clear后append未更换storage且capacity不变；
- frame0 time为10、target为0仍因lastAction-before-bracket顺序重放value4；
- cursor写1，表示scan抵达tail sentinel；
- currentTime最后写0。
