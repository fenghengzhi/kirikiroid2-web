# Motion.EmotePlayer #35–49 typed target 与 scale setter 精确复用四参考审计

## 结论

四份当前参考的 `Motion.EmotePlayer` registrar 在 member #35–49 之间没有 raw
callback，也没有把任何项直接落到一个不同 class 的 Engine member pointer。15 个脚本项
全部使用 typed NCBind descriptor，保存的目标均以 Primary 的 Engine-sized payload 为
`this`，member adjustment 全部为 0。

这段表面分成三种数据流：

1. #35–38 是实际存在的 Primary member wrapper，先从 payload 取 owned inner
   `Player *`，再进入 affine/camera/root body；
2. #39–48 直接读写 Engine-sized payload 内的 scale/trigger 字段；
3. #49 从同一 payload 内 copy-construct `_variableLabelsBase` Variant，setter member
   pointer 为空。

本轮发现的本地源码结构偏差是 #42–44：原生 property setter 与 #39–41 method
不是三个“实现相同但名字不同”的函数，而是 descriptor 中逐字相同的成员指针。
本地原有 `setHairScaleProp` / `setBustScaleProp` / `setPartsScaleProp` 是端口合成的
额外 facade，现已删除；三个 property 直接复用 `setHairScale` / `setBustScale` /
`setPartsScale`。

## 目标函数映射

| member | 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 35 | `setDrawAffineTranslateMatrix` | `0x67F2C8` | `0x562068` | `0x1001B6148` | `0x1B5EEC` |
| 36 | `getCameraOffset` wrapper | `0x67F2D0` | `0x562070` | `0x1001B6150` | `0x1B5F26` |
| 37 | `setCameraOffset` wrapper | `0x67F2D8` | `0x56207E` | `0x1001B6158` | `0x1B5F34` |
| 38 | `modifyRoot` wrapper | `0x67F2EC` | `0x56209C` | `0x1001B616C` | `0x1B5F56` |
| 39 | `setHairScale` | `0x67F300` | `0x5620AC` | `0x1001B619C` | `0x1B5F84` |
| 40 | `setPartsScale` | `0x67F308` | `0x5620B6` | `0x1001B61A4` | `0x1B5F8E` |
| 41 | `setBustScale` | `0x67F310` | `0x5620C0` | `0x1001B61AC` | `0x1B5F98` |
| 42 | `hairScale` get / set | `0x67F318` / `0x67F300` | `0x5620CA` / `0x5620AC` | `0x1001B61B4` / `0x1001B619C` | `0x1B5FA2` / `0x1B5F84` |
| 43 | `bustScale` get / set | `0x67F320` / `0x67F310` | `0x5620D4` / `0x5620C0` | `0x1001B61BC` / `0x1001B61AC` | `0x1B5FAC` / `0x1B5F98` |
| 44 | `partsScale` get / set | `0x67F328` / `0x67F308` | `0x5620DE` / `0x5620B6` | `0x1001B61C4` / `0x1001B61A4` | `0x1B5FB6` / `0x1B5F8E` |
| 45 | `debugPrint` get / set | `0x67F330` / `0x67F338` | `0x5620E8` / `0x5620EE` | `0x1001B61CC` / `0x1001B61D4` | `0x1B5FC0` / `0x1B5FC6` |
| 46 | `queuing` get / set | `0x67F344` / `0x67F34C` | `0x5620F6` / `0x5620FC` | `0x1001B61E0` / `0x1001B61E8` | `0x1B5FCE` / `0x1B5FD4` |
| 47 | `directEdit` get / set | `0x67F358` / `0x67F360` | `0x562104` / `0x56210A` | `0x1001B61F4` / `0x1001B61FC` | `0x1B5FDC` / `0x1B5FE2` |
| 48 | `selectorEnabled` get / set | `0x67F36C` / `0x67F374` | `0x562112` / `0x562118` | `0x1001B6208` / `0x1001B6210` | `0x1B5FEA` / `0x1B5FF0` |
| 49 | `variableKeys` get / null set | `0x67F380` / `0` | `0x562122` / `0` | `0x1001B621C` / `0` | `0x1B5FFA` / `0` |

## Registrar 证据锚点

下表是每项 stored-target reference 或 typed factory call 的 fresh disasm/decompile
锚点。Android arm64 的若干 template 被 LTO 内联，因此该列对内联项给出目标指针写入
位置；其余三端通常直接给出 factory 调用位置。

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|---:|
| 35 | `0x67DCC0` | `0x56167A` | `0x1001B5660` | `0x1B52A6` |
| 36 | `0x67DD18` | `0x561690` | `0x1001B5680` | `0x1B52C4` |
| 37 | `0x67DD54` | `0x5616A6` | `0x1001B56A0` | `0x1B52E2` |
| 38 | `0x67DDB4` | `0x5616BC` | `0x1001B56C0` | `0x1B5300` |
| 39 | `0x67DDCC` | `0x5616D4` | `0x1001B56E4` | `0x1B5320` |
| 40 | `0x67DDEC` | `0x5616EC` | `0x1001B5708` | `0x1B5340` |
| 41 | `0x67DE0C` | `0x561704` | `0x1001B572C` | `0x1B5360` |
| 42 | `0x67DE5C` | `0x56171E` | `0x1001B5754` | `0x1B5382` |
| 43 | `0x67DEE8` | `0x561736` | `0x1001B577C` | `0x1B53A4` |
| 44 | `0x67DF50` | `0x56174C` | `0x1001B57A4` | `0x1B53C6` |
| 45 | `0x67DFB8` | `0x561766` | `0x1001B57D0` | `0x1B53F2` |
| 46 | `0x67E048` | `0x56177E` | `0x1001B57FC` | `0x1B541E` |
| 47 | `0x67E0C0` | `0x561798` | `0x1001B5828` | `0x1B544A` |
| 48 | `0x67E134` | `0x5617B2` | `0x1001B5854` | `0x1B5476` |
| 49 | `0x67E19C` | `0x5617C8` | `0x1001B587C` | `0x1B5498` |

四端 descriptor family 与 member-pointer 形状一致：

| members | typed NCBind family | 关键边界 |
|---|---|---|
| 35 | six `double` -> Boolean | 少于六参报 `TJS_E_BADPARAMCOUNT`；surplus 忽略；成功发布 integer Boolean |
| 36 | no argument -> `tTJSVariant` | 所有非负 surplus 忽略；返回值走 Variant owner handoff |
| 37 | two `double` -> `void` | 少于两参报错；按 x、y 顺序转换；surplus 忽略 |
| 38 | no argument -> `void` | 所有非负 surplus 忽略 |
| 39–41 | one `double` -> `void` | 原样保留 double bit-pattern；无 clamp/dirty/controller side effect |
| 42–44 | `double` property | getter/setter adjustment 均为 0；setter 与 #39/#41/#40 目标逐字相同 |
| 45–48 | Boolean property | NCBind 先转换 Variant，native setter 不读取转换结果 |
| 49 | `tTJSVariant` property | getter adjustment 为 0；setter member pointer 全零 |

## #35–38 Player wrapper 数据流

四端 #35 都先从 Primary payload 读出 owned `Player *`，然后进入相同 affine body。
六个输入的顺序为 `m11, m21, m12, m22, m14, m24`；线性四项保持 double，平移
两项窄化 float。只有精确 identity（有符号零也等于零）清除 affine flag 并返回
false，其余包括 NaN 都置位并返回 true。

#36 每次调用 inner Player getter 新建一个 Dictionary，分别以 `x` / `y` 发布两个
float camera-offset 字段的 widened double。返回的是 owning Variant，不复用上一次
Dictionary。

#37 将两个 double 独立窄化 float 后直接写 inner Player 的 camera-offset x/y；没有
clamp、Engine dirty 写入，也不改 stereovision camera position。#38 沿 inner Player
当前 root 路径只设置 root delta dirty byte，不设置外层 Engine `_dirty`。

## #39–49 直接 payload 行为

#39–41 与 #42–44 访问同一组三个连续 double，字段顺序是 hair、parts、bust，property
发布顺序则是 hair、bust、parts。setter 是裸 double store；不会更新 metadata scale、
inverse combined scale、controller queue 或 Engine dirty flag。

#45–48 的 getter 读取四个相邻 Engine byte。四个 setter 的 C++ 类型仍是
`void (Class::*)(bool)`，所以 typed property wrapper 会执行 Variant-to-bool 转换；但优化后
目标体完全不读取该参数，始终写 true。`selectorEnabled` 还在每次 assignment 后无条件
调用 selector/variable-label 同步，即使字段本来已经为 true。

#49 通过 Variant copy constructor 返回 `_variableLabelsBase`。构造前它是 Void；metadata
reset 后每次读取 CopyRef 同一发布 Array，后续 selector sync 替换发布快照时，旧返回值仍
通过 refcount 保持旧 Array 生命周期。read-only descriptor 的 setter 槽在四端均为零。

## 本地源码修正与回归

- 删除 `EmotePlayer.h` 中三个原生不存在的 `set*ScaleProp` facade；
- `main.cpp` 的 #42–44 直接注册 `setHairScale` / `setBustScale` /
  `setPartsScale`，精确复用 #39/#41/#40 的 setter member；
- scale 回归改为从真实 Primary adaptor 依次对 `hairScale`、`bustScale`、
  `partsScale` 执行 `PropSet`，并验证三个 raw Engine double、non-finite/signed-zero
  保留、metadata pair 不变及 dirty 不变；
- 既有 camera、affine、Boolean trigger 与 variableKeys owner 回归继续覆盖本段其余边界。

## IDB 回写

四份 recovery IDB 完成并原位保存：

- 11 个此前匿名的 #36–38 target 统一改为 `_guess` 语义名；
- 7 个 two-double/no-argument typed factory helper 补语义名；
- #37–48 共 64 个 target prototype 按四 ABI 改回真实 `void`/`double`/`bool`
  member 形状；
- 60 个 registrar target/descriptor 点和 12 个 camera/root target 补 fresh 四参考注释；
- 每库新增四个分组 bookmark，并对 registrar 与 19 个 target 强制反编译回读。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 通过；唯一
  诊断是仓库既存 `_tss` literal-operator whitespace warning；
- `cmake --build --preset "Web Debug Build"` 完成 10 步编译并成功链接 `index.html`；
- 定向检查确认 `setHairScaleProp` / `setBustScaleProp` / `setPartsScaleProp` 已完全移除，
  #42–44 按 hair/bust/parts 顺序精确注册共享 setter；
- `git diff --check` 通过，仅输出工作区既有 LF-to-CRLF 提醒；
- 当前构建树没有可直接运行的 native motionplayer Catch2 executable，因此新增 adaptor
  断言已由完整测试翻译单元编译检查，但本轮未单独运行。

