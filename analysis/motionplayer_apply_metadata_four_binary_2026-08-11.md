# MotionPlayer metadata application 四参考二进制复原（2026-08-11）

## 结论

本轮重新反编译四个当前参考二进制中的 metadata 总装配函数，修正了本地从
旧 `libkrkr2.so` 阶段遗留的三个源级偏差：

1. 清空 metadata 自有状态之后，原版设置根镜像并**无条件调用完整的
   controller reset**，随后才以零步长推进 Player。本地原来漏掉该 reset。
2. `hairControl` / `partsControl` 共用 builder 的原生参数顺序是
   `(chainDeque, controlVariant, typeTag)`；本地原声明为
   `(chainDeque, typeTag, controlVariant)`。虽然旧调用者与被调者同时写错时
   能在本地自洽，但不等于原始源代码/ABI 结构。
3. 原版把输入转换为一个持有 dispatch 的 `ncbPropAccessor`，必选属性各用
   独立进程级 TJS hint，三个可选属性共用一个无 hint 的 Variant 槽；本地
   原来把各控制值分别保留到函数末尾，引用计数生命周期更长。

## 四端主链映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `resetMetadataState_guess` | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |
| `applyMetadata_guess` | `0x67A8B0` | `0x560020` | `0x1001B4468` | `0x1B3F58` |
| `buildVariableList_guess` | `0x667910` | `0x555FC0` | `0x1001A73C0` | `0x1A693C` |
| `buildBustControl_guess` | `0x6683F8` | `0x55659C` | `0x1001A7DDC` | `0x1A730C` |
| `buildChainControl_guess` | `0x668DB0` | `0x556B84` | `0x1001A87C0` | `0x1A7DCC` |
| `buildEyeControl_guess` | `0x669B5C` | `0x55739C` | `0x1001A91F4` | `0x1A8800` |
| `buildEyebrowControl_guess` | `0x669F7C` | `0x557618` | `0x1001A9540` | `0x1A8B68` |
| `buildMouthControl_guess` | `0x66A39C` | `0x557894` | `0x1001A988C` | `0x1A8ED0` |
| `buildTransitionControl_guess` | `0x66A8A4` | `0x557B84` | `0x1001A9C9C` | `0x1A9314` |
| `buildSelectorControl_guess` | `0x66ACDC` | `0x557E04` | `0x1001AA030` | `0x1A96D8` |
| `buildLoopControl_guess` | `0x66B860` | `0x558440` | `0x1001AAA8C` | `0x1AA158` |
| `buildClampControl_guess` | `0x66C23C` | `0x55892C` | `0x1001AB0A8` | `0x1AA760` |
| `buildMirrorControl_guess` | `0x66C744` | `0x558C24` | `0x1001AB4F4` | `0x1AABCC` |
| `buildInstantVariableList_guess` | `0x66CA2C` | `0x558DBC` | `0x1001AB6E4` | `0x1AAE18` |
| `buildTimelineControl_guess` | `0x66CBEC` | `0x558EB4` | `0x1001ABA30` | `0x1AB18C` |
| `syncSelectorControls_guess` | `0x66E0FC` | `0x559A8C` | `0x1001AC8A4` | `0x1AC0D0` |

紧邻的共同调用还包括：

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| 根 `flipX` setter | `0x6CA448` | `0x5926AE` | `0x10011D14C` | `0x11BB02` |
| 完整 controller reset | `0x66BF6C` | `0x558888` | `0x1001AB03C` | `0x1AA714` |
| Player 零步长 progress bridge | `0x6CFE34` | `0x595570` | `0x1001211C0` | `0x11FF88` |
| scale controller step | `0x663FD8` | `0x554014` | `0x1001A48C0` | `0x1A3E48` |

## 共同源级数据流

忽略 Variant/closure ABI 展开后，四端共同伪代码为：

```cpp
resetMetadataState();

tmp = copy(metadata);
tmp.ToObject();
metadataAccessor = ncbPropAccessor(tmp); // accessor 持有一个 dispatch 引用
tmp.Clear();                            // 第一次属性读取前释放临时 Variant

metadataMirror = getBool("mirror", mirrorHint);
mirrorChanged = requestedMirror != metadataMirror;
player->setFlipX(mirrorChanged);
resetControllers();
player->progress(0, 0.0);

meshScale = getReal("scale", scaleHint);
controllerScale = scaleController.step(0.0);
meshScaleReciprocal = 1.0 / (meshScale * controllerScale);

if (tryGetNoHint("variableList", optional)) buildVariableList(optional);

buildBustControl(getRequired("bustControl"));
buildChainControl(hairDeque, getRequired("hairControl"), 1);
buildChainControl(partsDeque, getRequired("partsControl"), 2);
buildEyeControl(getRequired("eyeControl"));
buildEyebrowControl(getRequired("eyebrowControl"));
buildMouthControl(getRequired("mouthControl"));
buildTransitionControl(getRequired("transitionControl"));

if (tryGetNoHint("selectorControl", optional)) buildSelectorControl(optional);

buildLoopControl(getRequired("loopControl"));
buildClampControl(getRequired("clampControl"));
buildMirrorControl(getRequired("mirrorControl"));

if (tryGetNoHint("instantVariableList", optional))
    buildInstantVariableList(optional);

buildTimelineControl(getRequired("timelineControl"));
syncSelectorControls();

// optional Variant 析构；metadataAccessor 析构并 Release dispatch
```

`resetMetadataState` 清空 metadata 拥有的十组 deque/控制器、HM6、镜像缓存、
HM3、变量容器等，但保留 active timeline label vector。紧随其后的
`resetControllers` 因而会用 HM3 `operator[]` 重新物化仍在 active vector 中的
缺失项。默认项的 `loopBegin == 0`，故 label 被保留，null blend controller
走共享 reset helper 的 null no-op 分支。这是本地漏掉 reset 时直接丢失的
可观察状态转换。

## 属性门与 TJS hint

| 类别 | 属性 | flags | hint | 缺失行为 |
|---|---|---:|---|---|
| 标量必选 | `mirror`, `scale` | `0` | 每项独立全局槽 | 继续做普通 Variant 数值/布尔转换 |
| 控制表必选 | `bustControl`, `hairControl`, `partsControl`, `eyeControl`, `eyebrowControl`, `mouthControl`, `transitionControl`, `loopControl`, `clampControl`, `mirrorControl`, `timelineControl` | `0` | 每项独立全局槽 | 仍调用相应 builder；没有可选跳过分支 |
| 可选 | `variableList`, `selectorControl`, `instantVariableList` | `TJS_MEMBERMUSTEXIST` | `nullptr` | PropGet 失败时跳过 builder |

四端的必选 hint 地址都形成连续的独立全局槽；三个可选调用直接经过 dispatch
vtable，并明确传 null hint。可选属性还复用同一个 Variant 存储：后一次成功
写入会释放/替换先前对象，失败则不调用 builder。源码现按这一结构复原。

必选控制值则在各自 builder 返回后立即析构，不会把 bust/hair/.../timeline
的全部 dispatch 同时保留到函数末尾。异常展开路径也会析构当前临时 Variant、
共享 optional Variant 和 metadata accessor。

## ABI 布局与 chain builder 参数顺序

| 字段/子对象 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Player 指针 | `+1064` | `+532` | `+696` | `+348` |
| requested/base/derived mirror | `+1156/+1157/+1158` | `+588/+589/+590` | `+788/+789/+790` | `+404/+405/+406` |
| scale controller | `+1080` | `+540` | `+712` | `+356` |
| metadata scale / reciprocal | `+1168/+1176` | `+600/+608` | `+800/+808` | `+412/+420` |
| type 1 chain deque | `+80` | `+40` | `+48` | `+24` |
| type 2 chain deque | `+160` | `+80` | `+96` | `+48` |

类型后重新反编译四端 call site 均稳定显示：

```cpp
buildChainControl(self, chainDeque, controlVariant, 1);
buildChainControl(self, chainDeque, controlVariant, 2);
```

因此本地方法签名已改为：

```cpp
void buildChainControl_guess(
    ChainDeque &chainNodes,
    const tTJSVariant &chainControl,
    int typeTag);
```

## 源码与 IDB 调整

### 2026-08-15：Primary `initPhysics` typed landing 补充

四端 Primary registrar 都把 #4 `initPhysics` 的成员代码指针直接设为
`applyMetadata_guess`，调整字段为零，并与 `draw`、`unserialize` 共用同一个
“一枚 `tTJSVariant` 按值参数、void 返回”的 typed NCBind 模板。不存在额外
`EmotePlayer::initPhysics` 转发函数。

因此上文伪代码中的 `tmp = copy(metadata)` 是 core 在 reset 之后做的**第二次**
复制：typed adapter 已经从 `param[0]` 建立并持有按值实参。源码现将
`applyMetadata_guess` 改为 `tTJSVariant metadata`，删除 facade 转发层，并以
脚本名 `initPhysics` 直接注册 Engine 成员。完整 registrar、Function dispatch、
arity/result/receiver 顺序与复制链见
`analysis/motionplayer_init_physics_typed_binding_owner_four_binary_2026-08-15.md`。

- 顶层函数从单一 Android 地址名改为 `applyMetadata_guess`，两个 EmotePlayer
  调用点同步改名。
- 13 个 builder 与 reset/sync 在四个 IDB 中统一语义命名；未知原始符号均
  保留 `_guess`。
- 四个 IDB 各完成 16/16 rename 与 16/16 source-level prototype 应用。
- 对 applyMetadata 与 chain builder 做了四端类型后新鲜反编译，确认 reset
  位置及 `(deque, value, tag)` 顺序未被错误 prototype 诱导。
- 修正 `motionplayer_set_mirror_four_binary_2026-08-11.md` 中“metadata 路径
  不走 reset tail”的过时结论。
- 新增 Catch2 回归用例：metadata 替换必须提交 active position controller，
  并把被 metadata reset 清掉、但仍留在 active vector 中的 timeline label
  重新物化为默认 HM3 项。

## 验证

- `cmake --build --preset "Web Debug Build"`：通过。
- `cmake --build --preset "Wasmtime Headless Debug Build"`：通过；普通与 guest
  两条对象路径均重新编译。
- `motionplayer-dll.cpp` 使用 Web Debug 完整编译参数做 `-fsyntax-only`：通过；
  仅有仓库既有 `_tss` 空白弃用警告。
- Windows 原生测试目录仍缺少生成的 `build.ninja`，因此本轮回归用例完成
  编译验证但未运行原生 Catch2 executable。
