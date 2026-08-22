# MotionPlayer Eye/Eyebrow 真实构造函数、标量写集合与异常生命周期四参考复原（2026-08-15）

## 结论

本轮纠正了 Eye/Blink 与 Eyebrow 两个 controller 的一项源码结构偏差。旧 portable 代码先
执行无参默认构造，再调用一个自由函数 `*_ctor(self, dict)` 填 metadata；同时用默认成员
初始化把所有 scalar 预先清零。四个当前 1.3.9 参考共同证明，原实现不是这种两阶段结构，
而是两个各自接收 metadata dict 的真实 C++ 构造函数。

四端共同的构造写集合也不是“整个 scalar tail 归零”。容器完成构造后只显式写：

- Eye：`trackState=0`、`trackTarget=0`、`trackDir=0`、`blinkPhase=0`；
- Eyebrow：`trackState=0`、`trackTarget=0`、`trackDir=0`；
- 两者的 `trackValue` 稍后都由 `beginFrame` 转 float 写入；Eye 还由同一个值写
  `blinkPos`，并由共享 RNG 写 `blinkTimer`；
- `mesh.trackResolvedSpan`、`trackSpan`、`trackAccum`、`trackInvDur`、`trackPow` 在构造
  期间均保持未写。它们由 resolver/track setup 在把状态推进到 active 之前写完。

因此删除这些字段的默认 `=0` 不是微观优化，而是恢复 shipped constructor 的精确
observable write set。任何调试器、sanitizer、placement-new 后内存观察或异常路径都不应
看见原实现没有执行的额外 scalar stores。

## 四平台函数映射

| 参考二进制 | Blink ctor | Eyebrow ctor | Eye builder | Eyebrow builder |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x65FD48` | `0x661BEC` | `0x669B5C` | `0x669F7C` |
| Android ARMv7 | `0x551B34` | `0x552CDC` | `0x55739C` | `0x557618` |
| iOS ARM64 | `0x1001A1C8C` | `0x1001A31F4` | `0x1001A91F4` | `0x1001A9540` |
| iOS ARMv7 | `0x1A0E50` | `0x1A2560` | `0x1A8800` | `0x1A8B68` |

四份 IDB 中继续使用 `EmoteBlinkController_ctor_guess` 与
`EmoteEyebrowController_ctor_guess`。`_guess` 是必须保留的保守后缀：二进制已经 stripped，
当前名字表达已证实的职责，不声称找回作者符号。

## 共同源码级构造控制流

### Blink / Eye

四端 ABI 展开可统一成：

```cpp
EmoteBlinkController::EmoteBlinkController(const Variant& dict)
    : valueTrack12B(), valueTrack8B(), mesh(),
      trackState(0), trackTarget(0.0f), trackDir(0.0f),
      blinkPhase(0) {
    beginFrame = getInt(dict, "beginFrame");
    endFrame = getInt(dict, "endFrame");
    blinkIntervalMin = float(getReal(dict, "blinkIntervalMin"));
    blinkIntervalMax = float(getReal(dict, "blinkIntervalMax"));
    blinkFrameCount = float(getReal(dict, "blinkFrameCount"));
    blinkEnabled = getBool(dict, "blinkEnabled") & 1;

    trackValue = float(beginFrame);
    blinkPos = float(beginFrame);
    blinkTimer = blinkIntervalMin
               + (blinkIntervalMax - blinkIntervalMin) * sharedRandom();

    edgeTable = convertEdgeArrayToFloatPairs(dict.edge);
    nodeRows = convertNodeArrayToFloatVectors(dict.node);
}
```

关键次序是 scalar metadata/seed/RNG 先于 edge/node 建表。2026-08-16 fresh 四端
source-identity 复核进一步证明：root、edge、node、逐项 pair/row 都是实际
`ncbPropAccessor`，不是 raw `MotionDispatch` helper 的源码结构；每个返回属性的临时
Variant 在对应 accessor 构造完成后立即析构，pair/row accessor 按迭代释放，长生命周期
accessor 在构造尾逆序释放。portable 端已据此纠正，详见
`analysis/motionplayer_emote_controller_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

### Eyebrow

```cpp
EmoteEyebrowController::EmoteEyebrowController(const Variant& dict)
    : valueTrack12B(), valueTrack8B(), mesh(),
      trackState(0), trackTarget(0.0f), trackDir(0.0f) {
    beginFrame = getInt(dict, "beginFrame");
    edgeTable = convertEdgeArrayToFloatPairs(dict.edge);
    nodeRows = convertNodeArrayToFloatVectors(dict.node);
    trackValue = float(beginFrame);
}
```

Eyebrow 没有读取 Blink 的 end/interval/frame-count/enabled 字段，也没有调用共享 RNG。
`trackValue` 在 edge/node 建表完成后才写；这项次序在四端一致。

## ABI 布局与原生未写字段

### Blink

| ABI | `sizeof` | primary/secondary/resolver | resolved | state/value/target/dir | span/accum/inv/pow | blink tail 起点 |
| --- | ---: | --- | ---: | --- | --- | ---: |
| Android A64 / libstdc++ | `0x170` | `+0/+0x50/+0xA0` | `+0x120` | `+0x128/+0x12C/+0x130/+0x134` | `+0x138/+0x13C/+0x140/+0x144` | `+0x148` |
| Android A32 / libstdc++ | `0xD8` | `+0/+0x28/+0x50` | `+0x90` | `+0x94/+0x98/+0x9C/+0xA0` | `+0xA4/+0xA8/+0xAC/+0xB0` | `+0xB4` |
| iOS A64 / libc++ | `0x110` | `+0/+0x30/+0x60` | `+0xC0` | `+0xC8/+0xCC/+0xD0/+0xD4` | `+0xD8/+0xDC/+0xE0/+0xE4` | `+0xE8` |
| iOS A32 / libc++ | `0xA8` | `+0/+0x18/+0x30` | `+0x60` | `+0x64/+0x68/+0x6C/+0x70` | `+0x74/+0x78/+0x7C/+0x80` | `+0x84` |

“blink tail 起点”是 `beginFrame`；随后依次为 `endFrame`、`blinkPhase`、三个 interval/
frame-count float、timer、position、enabled byte。四端只有 `blinkPhase` 在 metadata 读取前
清零，其余 tail 都由属性、begin seed 或 RNG 赋值。

### Eyebrow

| ABI | `sizeof` | primary/secondary/resolver | resolved | state/value/target/dir | accum/span/pow/inv | beginFrame |
| --- | ---: | --- | ---: | --- | --- | ---: |
| Android A64 / libstdc++ | `0x150` | `+0/+0x50/+0xA0` | `+0x120` | `+0x128/+0x12C/+0x130/+0x134` | `+0x138/+0x13C/+0x140/+0x144` | `+0x148` |
| Android A32 / libstdc++ | `0xB8` | `+0/+0x28/+0x50` | `+0x90` | `+0x94/+0x98/+0x9C/+0xA0` | `+0xA4/+0xA8/+0xAC/+0xB0` | `+0xB4` |
| iOS A64 / libc++ | `0xF0` | `+0/+0x30/+0x60` | `+0xC0` | `+0xC8/+0xCC/+0xD0/+0xD4` | `+0xD8/+0xDC/+0xE0/+0xE4` | `+0xE8` |
| iOS A32 / libc++ | `0x88` | `+0/+0x18/+0x30` | `+0x60` | `+0x64/+0x68/+0x6C/+0x70` | `+0x74/+0x78/+0x7C/+0x80` | `+0x84` |

Eyebrow 的 curve 顺序与 Blink 不同：Blink 是 span/accum/inv/pow，Eyebrow 是
accum/span/pow/inv。这个差异早已由 step caller 证明，本轮构造审计进一步确认两类 object
不是一个强行共享 scalar base 的布局。

## 构造器精确清零指令

容器 header 的清零/初始化受 STL 实现影响：Android libstdc++ 先逐个 memclear header，
再调用会 eagerly 分配 map/block 的 deque initializer；iOS libc++ 以整片零写建立 lazy deque/
vector header。两种展开都严格止于 `trackResolvedSpan` 之前，不会顺带清掉该 float。

### Blink scalar stores

| 参考 | state=0 | target/dir=0 | blinkPhase=0 |
| --- | ---: | ---: | ---: |
| Android A64 | `0x65FDF0` | `0x65FDF4` | `0x65FDF8` |
| Android A32 | `0x551B9A` | `0x551B9E` | `0x551BA2` |
| iOS A64 | `0x1001A1CB8` | `0x1001A1CC0` | `0x1001A1CC4` |
| iOS A32 | `0x1A0E84` | `0x1A0E88` / `0x1A0E8C` | `0x1A0E9A` |

Android 两端还分别在 `0x65FDE8..0x65FDEC` 与 `0x551B92..0x551B96`
清空 `outputRows` vector header；这不是 `trackResolvedSpan` 的写入。

### Eyebrow scalar stores

| 参考 | state=0 | target/dir=0 |
| --- | ---: | ---: |
| Android A64 | `0x661C90` | `0x661C94` |
| Android A32 | `0x552D3E` | `0x552D42` |
| iOS A64 | `0x1001A3220` | `0x1001A3224` |
| iOS A32 | `0x1A2594` | `0x1A2598` / `0x1A25A6` |

这些 store 与随后 beginFrame/edge/node 数据流之间没有对 resolved/span/accum/inv/pow 的
隐藏宽写。A64/A32、libstdc++/libc++ 四种展开共同排除了“只是反编译漏显示默认清零”的
可能。

## 延迟字段的 first-write / state gate

`trackResolvedSpan` 的 first write 在 resolver wrapper，而不是 controller constructor：

| 参考 | resolver | success span store | fallback zero store |
| --- | ---: | ---: | ---: |
| Android A64 | `0x65F35C` | `0x65F494` | `0x65F66C` |
| Android A32 | `0x5514C8` | `0x551576` | `0x55164C` |
| iOS A64 | `0x1001A15DC` | `0x1001A16B0` | `0x1001A1768` |
| iOS A32 | `0x1A0768` | `0x1A080A` | `0x1A089C` |

当 `trackState==0` 且 primary track 非空时，两类 step 均按以下次序运行：

```cpp
resolve(mesh, trackValue, keyframe.target, valueTrack8B); // always writes resolved
trackAccum = 0;
trackSpan = mesh.trackResolvedSpan;
trackInvDur = 1 / keyframe.duration;
trackPow = raw keyframe power bits;
trackState += 1;
```

Blink setup 地址为：

| 参考 | resolver call | curve setup stores |
| --- | ---: | --- |
| Android A64 | `0x6610B4` | `0x6610C4..0x6610D4` |
| Android A32 | `0x5524C4` | `0x5524D0..0x5524DE` |
| iOS A64 | `0x1001A284C` | `0x1001A2854..0x1001A2860` |
| iOS A32 | `0x1A1A5E` | `0x1A1A68..0x1A1A72` |

Eyebrow setup 地址为：

| 参考 | resolver call | curve setup stores |
| --- | ---: | --- |
| Android A64 | `0x662A5C` | `0x662A64..0x662A90` |
| Android A32 | `0x5532E0` | `0x5532EE..0x553300` |
| iOS A64 | `0x1001A3994` | `0x1001A399C..0x1001A39B4` |
| iOS A32 | `0x1A2CF8` | `0x1A2D02..0x1A2D1A` |

状态 2 的 active curve 才读取这些字段。新对象的 state 明确为 0；primary track 为空时
直接跳过 setup/active，因而原生未初始化字段没有 normal-path 先读。portable 实现保持
完全相同的 gate，并未在测试中观察或断言这些字段的构造后 bit pattern。

## Builder new-expression 与异常边界

四端两个 builder 均显示：

```cpp
if (elem.enabled) {
    raw = new CategoryController(elem);
    categoryDeque.emplace_back(raw); // target owns raw; label initially empty
    categoryDeque.back().label = elem.label;
    variableControllerRefs[label] = {categoryType, metadataIndex};
}
```

allocation size 对应上述 `sizeof` 表。controller 构造失败时，new-expression landing
释放尚未成功构造的 allocation；构造函数自身按已完成成员逆序清理。构造成功后没有局部
`unique_ptr` owner，deque map/block growth 若抛异常，raw controller 不会被回收。这一
shipped leak boundary 已在 `motionplayer_eye_entry_owner_emplace_four_binary_2026-08-13.md`
单独闭合，本轮真实构造函数迁移后不再需要 pending guard workaround。

正常 element 销毁和构造展开失败的成员逆序为：

```text
outputRows -> nodeRows -> edgeTable -> valueTrack8B -> valueTrack12B
```

对应 payload/owner 销毁锚点：

| 参考 | Blink payload/owner destruction | Eyebrow payload/owner destruction |
| --- | ---: | ---: |
| Android A64 | `0x680630` | `0x6808BC` |
| Android A32 | `0x562ED8` | `0x563070` |
| iOS A64 | range 内联于 `0x1001B73DC` | range 内联于 `0x1001B7514` |
| iOS A32 | range 内联于 `0x1B6EEC` | range 内联于 `0x1B6FC0` |

portable implicit member destructor 与这条逆声明顺序一致。

## 本地实现对照

- `EmoteBlinkController` 与 `EmoteEyebrowController` 在 struct 内声明
  `explicit Controller(const tTJSVariant&)`，定义迁入各自 `.cpp`；
- 原 `EmoteBlinkController_ctor(self, dict)` / `EmoteEyebrowController_ctor(self, dict)`
  自由函数入口删除；
- initializer list 只写四端共同证明的 scalar 集合；metadata/RNG/body assignment 保持
  原先已经恢复的读取次序；
- 两个 controller 以及共享 `EmoteMeshResolverState::trackResolvedSpan` 的额外默认成员
  初始化全部删除；
- `EmoteEngine` builder 改成直接 `new Controller(elem)`，成功返回后立即以 raw pointer
  emplace，构造失败与 growth failure 的 ownership 分界不变；
- 测试中的独立 controller 也通过空 Variant 调用真实构造函数。测试只观察 constructor
  明确写入的 state/value 与随后业务路径写入的字段，不读延迟未初始化 scalar；
- 已同步修正 eye-entry owner 文档中关于“分离 helper + pending guard”的过时现状描述，
  并在 mesh-resolver 文档补记 resolved span 的 constructor-uninitialized 边界。

## 验证状态

- 四个 Blink ctor、四个 Eyebrow ctor、四个 Eye builder、四个 Eyebrow builder已在本轮
  重新反编译；四个 resolver wrapper、八个 controller step 也重新核对 first-write/gate；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用既有 Emscripten response file
  执行 `-fsyntax-only`：通过，仅有仓库既有 `_tss` literal-operator 弃用 warning；
- `cmake --build --preset "Web Debug Build"`：13-step 增量构建与最终链接通过；仅有仓库
  既有 `_tss`、`imagepacker.h` attribute、pthread-growth/JSPI 等 warning；
- 四份 recovery IDB 已写入 ctor exact-write、resolver first-write、state-gated setup 与
  payload destruction 注释；Android A64 payload destructor 已保守命名为
  `EmoteBlinkController_dtor_guess`；构造写集合和 resolved first-write 书签已添加；
- 四份 recovery IDB 均已原位保存成功。
