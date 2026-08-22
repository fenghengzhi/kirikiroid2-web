# Motion.EmotePlayer #20–34 typed property、精确 alias 与 Variant 发布四参考审计

## 结论

四份当前参考的 `Motion.EmotePlayer` registrar 在 member #20–34 连续注册 15 个
typed property descriptor，所有 getter/setter member adjustment 均为 0。这一段没有
raw callback，也没有直接保存继承的 `EmoteEngine` member；目标都以 Primary 的
Engine-sized payload 为 `this`，再读写 embedded `Player` 或包装 Player 返回值。

本轮确认了三组会影响可观察脚本行为和源码结构的精确复用：

1. #23 `motionKey` 与 #24 `project` 的 getter、setter 两个 member pointer 都逐字相同；
   setter 的 native 参数是 `ttstr`，所以任意脚本 Variant 会先被 NCBind 转成字符串，再
   构造 `tvtString` Variant 覆盖 Player 的 persistent motion-context owner；
2. #29 `frameLastTime` 与 #31 `lastTime` 逐字复用同一个 raw-frame getter，#30
   `frameLoopTime` 与 #32 `loopTime` 同样复用另一个 raw-frame getter。Primary 的短名
   不执行 `Motion.Player` 表面上“正数 frame -> milliseconds”的换算；
3. #34 不是把递归 core 的 `uint32_t` 直接交给 property adapter。Primary target 自己把
   count 经 signed `tjs_int` 转换后构造 `tvtInteger` Variant，再由 read-only property
   descriptor 做 Variant owner handoff。

因此本地原有的 `project` 专用 facade、`lastTime`/`loopTime` 专用 facade，以及 #34 的
`uint32_t` Primary 返回签名都不对应参考源码结构，现已删除或改正。

## 目标函数映射

同一格中的两个地址依次为 getter / setter；`—` 表示 read-only descriptor 的空 setter。

| member | 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 20 | `completionType` get / set | `0x67F02C` / `0x67F038` | `0x561E50` / `0x561E5A` | `0x1001B5F24` / `0x1001B5F30` | `0x1B5BD6` / `0x1B5BE0` |
| 21 | `chara` get / set | `0x67F044` / `0x67C750` | `0x561E64` / `0x561044` | `0x1001B5F3C` / `0x1001B4FD8` | `0x1B5BEA` / `0x1B4BFC` |
| 22 | `motion` get / set | `0x67F068` / `0x67F08C` | `0x561E88` / `0x561EAC` | `0x1001B5F60` / `0x1001B5F84` | `0x1B5C0E` / `0x1B5C34` |
| 23 | `motionKey` get / set | `0x67F128` / `0x67C6C0` | `0x561F18` / `0x560FD4` | `0x1001B5FEC` / `0x1001B4F68` | `0x1B5CF4` / `0x1B4B38` |
| 24 | `project` get / set | `0x67F128` / `0x67C6C0` | `0x561F18` / `0x560FD4` | `0x1001B5FEC` / `0x1001B4F68` | `0x1B5CF4` / `0x1B4B38` |
| 25 | `maskMode` get / set | `0x67F1A4` / `0x67F1B0` | `0x561F74` / `0x561F7E` | `0x1001B6048` / `0x1001B6054` | `0x1B5DA4` / `0x1B5DAE` |
| 26 | `meshDivisionRatio` get / set | `0x67F1BC` / `0x67F1C8` | `0x561F88` / `0x561F96` | `0x1001B6060` / `0x1001B606C` | `0x1B5DB8` / `0x1B5DC6` |
| 27 | `outline` get / set | `0x67F1D4` / `0x67F1E4` | `0x561FA4` / `0x561FB8` | `0x1001B6078` / `0x1001B6088` | `0x1B5DD4` / `0x1B5DE8` |
| 28 | `priorDraw` get / set | `0x67F258` / `0x67F264` | `0x562010` / `0x56201A` | `0x1001B60DC` / `0x1001B60E8` | `0x1B5E94` / `0x1B5E9E` |
| 29 | `frameLastTime` get / — | `0x67F274` / `0` | `0x562024` / `0` | `0x1001B60F4` / `0` | `0x1B5EA8` / `0` |
| 30 | `frameLoopTime` get / — | `0x67F280` / `0` | `0x562032` / `0` | `0x1001B6100` / `0` | `0x1B5EB6` / `0` |
| 31 | `lastTime` get / — | `0x67F274` / `0` | `0x562024` / `0` | `0x1001B60F4` / `0` | `0x1B5EA8` / `0` |
| 32 | `loopTime` get / — | `0x67F280` / `0` | `0x562032` / `0` | `0x1001B6100` / `0` | `0x1B5EB6` / `0` |
| 33 | `bounds` get / — | `0x67F28C` / `0` | `0x562040` / `0` | `0x1001B610C` / `0` | `0x1B5EC4` / `0` |
| 34 | `processedMeshVerticesNum` Variant get / — | `0x67F294` / `0` | `0x56204E` / `0` | `0x1001B6114` / `0` | `0x1B5ED2` / `0` |

## Registrar 证据锚点

下表给出每个 property descriptor 的 fresh stored-target/factory 锚点。每个锚点都已
结合周围 name Variant、getter、setter、两字 member-pointer adjustment 和 property
factory 调用复核，而不是只凭相邻注册顺序推断。

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|---:|
| 20 | `0x67D618` | `0x5614D4` | `0x1001B53F0` | `0x1B5078` |
| 21 | `0x67D6A0` | `0x5614F4` | `0x1001B541C` | `0x1B50A4` |
| 22 | `0x67D730` | `0x561514` | `0x1001B5448` | `0x1B50D0` |
| 23 | `0x67D78C` | `0x561536` | `0x1001B547C` | `0x1B50FE` |
| 24 | `0x67D820` | `0x56154C` | `0x1001B54A0` | `0x1B5118` |
| 25 | `0x67D884` | `0x56156C` | `0x1001B54CC` | `0x1B5144` |
| 26 | `0x67D8EC` | `0x56158C` | `0x1001B54F8` | `0x1B5170` |
| 27 | `0x67D96C` | `0x5615AC` | `0x1001B5524` | `0x1B519C` |
| 28 | `0x67D9EC` | `0x5615CC` | `0x1001B5550` | `0x1B51C8` |
| 29 | `0x67DA70` | `0x5615E8` | `0x1001B557C` | `0x1B51EC` |
| 30 | `0x67DAD0` | `0x561604` | `0x1001B55A8` | `0x1B5210` |
| 31 | `0x67DB54` | `0x56161A` | `0x1001B55CC` | `0x1B522A` |
| 32 | `0x67DBB0` | `0x561630` | `0x1001B55F0` | `0x1B5244` |
| 33 | `0x67DBF8` | `0x56164A` | `0x1001B5618` | `0x1B5266` |
| 34 | `0x67DC58` | `0x561664` | `0x1001B5640` | `0x1B5288` |

## Typed family、数据流与边界

| members | native property 类型 | 目标行为 |
|---|---|---|
| 20 | signed `tjs_int` | 读写 Player completion scalar，不做 unsigned 重解释 |
| 21 | `ttstr` | getter 返回当前 chara；setter 以 flags 0 进入 Player chara coordinator |
| 22 | `ttstr` | getter 返回当前 motion；setter 以 flags 0 进入 Player play coordinator |
| 23–24 | `tTJSVariant` getter / `ttstr` setter | 两个脚本名逐字复用同一目标；setter 把转换后的字符串包装为 persistent String Variant |
| 25 | signed `tjs_int` | 读写 Player mask scalar |
| 26 | `double` | raw load/store；不 clamp，不窄化为 float |
| 27 | `tTJSVariant` | getter CopyRef，setter copy-assign persistent outline owner |
| 28 | Boolean | getter 发布 Boolean；setter真实消费 NCBind 转换后的 bool |
| 29–32 | `double` read-only | 两对脚本名各复用一个 raw-frame getter；setter pointer 为 0 |
| 33 | `tTJSVariant` read-only | wrapper 构造/返回 bounds Variant；setter pointer 为 0 |
| 34 | `tTJSVariant` read-only | recursive `uint32_t` core -> signed `tjs_int` -> `tvtInteger` Variant；setter pointer 为 0 |

typed property adapter 先按目标签名完成 Variant 转换，再调用 native member。最显著的
可观察结果是给 `project` 或 `motionKey` 赋 Integer、Real、Boolean 等值时，持久槽内的
类型不是原输入类型，而是 `tvtString`；从另一个 alias 读取会看到同一字符串状态。getter
返回的是 persistent Variant 的 CopyRef，覆盖槽位前已经取出的 Variant 仍独立保持旧引用。

#28 与相邻 #45–48 的“一写即 true”trigger property 不同：`priorDraw=false` 会实际把
Player byte 写为 false。#26 也不是 metadata mesh-resolution重建入口，仅保存完整 double。

## #34 signed publication 边界

四端 #34 target 都先调用递归 Player count core，再在 Primary wrapper 中按 signed
`tjs_int` 语义发布 Integer Variant。64 位目标把 32-bit count sign-extend 到 Variant 的
integer payload；32 位目标直接写同一 32-bit payload。由此，高位为 1 的原生 count 在
脚本侧表现为负整数，而不是无符号大数。当前单元场景覆盖零值与 Variant type；由于构造
一个实际超过 `INT32_MAX` 的渲染树成本过高，本轮未合成该极端运行时图，但源码保留了
四端目标明确出现的 signed cast。

## Android arm64 IDA 函数边界伪影

Android arm64 的 #23/#24 setter 入口 `0x67C6C0` 有独立 stack prologue，并在前一条路径
已经于 `0x67C6B0` return 后开始。它依次读取 Primary 的 Player、从 `ttstr` 构造 type=2
String Variant、copy-assign persistent motion-context 字段、销毁临时量并返回。

当前恢复 IDB 的异常区域错误地把该入口并入从 `0x67C4AC` 开始的
`ObjectRefVector_copyAssign_guess` 函数范围。为避免破坏异常处理边界，本轮没有强行拆分
函数；只把真实入口 code label 改为 `EmotePlayer_setMotionKeyProject_guess` 并追加边界
纠正注释。另三份参考均把该 setter 暴露为独立函数，足以交叉确认其身份与范围。

## 本地源码修正与回归

- Primary `setMotionKey` 的参数从 `tTJSVariant` 改回 `ttstr`，内部构造 String Variant 后
  调用 Player persistent motion-context copy assignment；
- 删除 Primary 的 `setProject/getProject` 合成 facade；`main.cpp` 的 #23/#24 精确注册
  同一个 `getMotionKey/setMotionKey` pair；
- 删除 Primary 的 `getLastTime/getLoopTime` 合成 facade；#29/#31 共同注册
  `getFrameLastTime`，#30/#32 共同注册 `getFrameLoopTime`；
- #34 的 Primary 返回类型改为 `tTJSVariant`，在 wrapper 中显式执行 signed
  `tjs_int` publication；
- 新增真实 Primary adaptor 回归：整数经 `project`/`motionKey` assignment 后变为字符串，
  两个 alias 双向可见；四个 time 名按两对精确相等；#34 direct/adaptor 都发布
  `tvtInteger`。

## IDB 回写

四份 recovery IDB 完成并原位保存：

- 28 个此前匿名的 #20/#21/#22 getter 与 #25 getter/setter target 统一改为 `_guess`
  语义名；
- Android arm64 `0x67C6C0` 独立 code label 改为
  `EmotePlayer_setMotionKeyProject_guess`；
- 40 个 scalar/double/Boolean target prototype 按四 ABI 修正；
- 60 个 registrar descriptor 点、24 个 target 和四个 motion-context setter 入口追加
  fresh 四参考注释；
- 每库新增四个分组 bookmark，并强制反编译回读 registrar 与 20 个 target/入口。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 通过；唯一
  诊断是仓库既存 `_tss` literal-operator whitespace warning；
- `cmake --build --preset "Web Debug Build"` 完成 10 步编译并成功链接 `index.html`；
- 定向检查只截取 `NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)` 的 Primary 注册块，确认
  #23/#24 和 #29–32 精确复用，且不存在三个旧 facade 注册；
- 当前构建树没有可直接运行的 native motionplayer Catch2 executable，因此新增 adaptor
  断言已由完整测试翻译单元编译检查，但本轮未单独运行。

