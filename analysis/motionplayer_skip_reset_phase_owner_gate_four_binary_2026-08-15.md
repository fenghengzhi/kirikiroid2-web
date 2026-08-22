# Skip/reset phase order and active blend-owner gate — four-reference reconstruction

Date: 2026-08-15

本纵切面 fresh 复核 Motion/D3D `skip()` 到 Engine完整controller reset链，重点闭合
active timeline operator[]、blend owner null gate、direct owner与spring phase次序，以及
后续controller队列提交顺序。

## 四端入口

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| D3D skip facade | `0x530E24` | `0x49500E` | `0x100233458` | `0x23219E` |
| Engine reset main | `0x66BF6C` / `0x2D0` | `0x558888` / `0xA4` | `0x1001AB03C` / `0x6C` | `0x1AA714` / `0x4A` |
| active timeline phase | `0x6670F0` / `0x10C` | `0x555B4C` / `0x70` | `0x1001A6844` / `0xF4` | `0x1A5FC0` / `0xB4` |
| HM3 subscript | `0x685060` | `0x5669AC` | `0x1001A6938` | `0x1A6074` |

D3D facade仅解析primary Engine并直调reset；无参数、result、null shell gate或modified
写入。Motion.EmotePlayer注册面直接绑定同一Engine reset主体，没有独立Motion wrapper。

## active timeline phase

```cpp
index = 0;
while (index < activeLabels.size()) {
    TimelineState &state = timelineStates[activeLabels[index]];
    if (state.loopBegin >= 0.0) {
        if (state.blendController)
            resetVarController(state.blendController);
        ++index;
    } else {
        applyTimelineWindow(state, true, state.lastTime);
        activeLabels.erase(activeLabels.begin() + index);
    }
}
```

四端都用inserting HM3 subscript，不是`find/at`。stale active label会先建立拥有key的
默认state（`loopBegin=0.0`、`blendWeight=1.0f`、null owners），随后走loop分支。重要
细节是 null test位于active phase：blend owner为空时根本不调用reset helper。

ordered `loopBegin >= 0.0` 使 `-0.0`留在active，NaN进入non-loop window/erase路径。
non-loop window若抛异常，label尚未erase；此前其他active项可能已reset/erased且不回滚。
成功window后vector erase搬移后继owner并释放尾项，index不增。

## main phase顺序

active phase完成后，四端保持以下共同顺序：

1. direct outer-force Var owners：bust、hair、parts；
2. hair/parts spring nodes：`spring.firstFlag=1`，随后entry `initFlag=1`；
3. bust-chain-1 nodes同样写两byte；
4. bust-chain-2 nodes同样写两byte；
5. blink controllers；
6. eyebrow controllers；
7. mouth controllers；
8. selector controllers；
9. transition Var controllers；
10. position、scale、angle、color direct controllers。

Android arm64内联多数deque walk；Android armv7/iOS把阶段outline成小helper。helper边界
不同但调用次序、每项owner读取和提交语义一致。

selector早于transition可见：selector应用最终选择可能向借用的transition controller
排队，紧随其后的transition reset立即把该队列终点提交并清空。

## controller/owner边界

- direct outer-force/base owners、node spring owner和各deque entry controller均由Engine
  构造不变量保证非null；main phase不额外防御。
- Var reset有队列时提交最后key的所有channel并clear；无队列但active时提交target；
  idle+empty不写。
- mouth有队列时提交最后endRad并clear；active+empty提交endVal。
- angle有队列时提交最后endRad且不normalize；active+empty才以反复加/减截断常量
  `6.2832f` normalize。NaN直接发布为NaN；正/负Infinity会停在相应while循环中，原版
  无finite guard。
- 每阶段即时提交；任何较后owner/reset异常不回滚先前timeline erase、spring byte、
  current value或queue clear。

## 本地对齐

本地operator[]、erase loop和主phase次序原已与四端一致。本轮将loop-state
blend-controller的null gate从本地容错reset helper调用外提到Engine active phase，恢复
四端实际调用结构。已有回归会以缺失active label触发HM3 materialization，并验证新state
保持active且blend owner仍null。

验证结果：Emscripten syntax-only测试翻译单元通过（仅既有 literal-operator弃用
warning），`Web Debug Build` 以3个增量步骤完成最终链接，目标 `git diff --check`
通过。
