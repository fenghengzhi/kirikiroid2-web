# MotionPlayer 三项原生运行时偏差的四二进制复核（2026-08-28）

## 1. 范围与取证状态

本轮只处理 macOS native suite 暴露的三项偏差：

1. `DrawDeviceManagerItem::IsVisible()` 对 `drawvisible` 的 Variant 真值转换；
2. `Motion.EmotePlayer` typed Factory 的脚本参数计数；
3. `EmotePlayer` 四个 Boolean trigger property，尤其
   `selectorEnabled` 的同步前置状态。

原生 `mcp__idalib__*` transport 恢复后，四个配套 IDB 均重新打开并经
`server_health` 验证 `status=ok`、`hexrays_ready=true`。每个目标均完成本轮 fresh
decompile；关键 wrapper 又完成全函数 disassembly。四个 IDB 已追加注释与书签、保存并
正常关闭。

## 2. `drawvisible`：完整映射与共同控制流

字符串不是 ASCII。四端都只有精确 UTF-16LE（含终止符）命中，ASCII 与 UTF-32LE 均为
零命中；本轮还用 `get_bytes` 读取了完整字节。

| 目标 | UTF-16LE `drawvisible` | owner helper | 真值转换形态 |
|---|---:|---:|---|
| Android arm64-v8a | `0x14BEF90` | `sub_532BCC@0x532BCC` | helper 内联完整 type switch |
| Android armv7 | `0x49671C` | `sub_4966A4@0x4966A4` | `sub_496CC4@0x496CC4` |
| iOS arm64 | `0x10197030E` | `sub_1002351E0@0x1002351E0` | `sub_100037640@0x100037640` |
| iOS armv7 | `0x17626BA` | `sub_233EE8@0x233EE8` | `sub_3589C@0x3589C` |

共同伪代码：

```text
if owner == null:
    return false

value = Void
status = owner.PropGet(MEMBERMUSTEXIST, u"drawvisible", hint, &value, owner)
if status == MEMBERNOTFOUND:
    return true

truth = value.operator bool()
destroy(value)
return truth
```

`operator bool()` 的完整分支在四端一致：Void=false；Object/Octet 按指针是否为 null；
String 先做整数解析；Integer 与 Real 分别和零比较。除 `MEMBERNOTFOUND` 外，PropGet 的
其他成功或失败 status 都不改变这条路径；dispatch 即使返回失败，只要写过 Variant，仍
转换那个值。异常沿原生 EH 路径传播。

本地偏差来自重载解析：本地 `value` 是 non-const，`static_cast<bool>(value)` 可优先选择
non-const Object conversion 后再做 pointer-to-bool；Integer 因而抛出 Integer→Object。
显式 `value.operator bool()` 才逐项对应上述四端 type switch。

## 3. `Motion.EmotePlayer` typed Factory

| 目标 | descriptor invoke | 完整指令数 |
|---|---:|---:|
| Android arm64-v8a | `motion_EmotePlayer_factory_invoke@0x689CA4` | 45 |
| Android armv7 | `motion_EmotePlayer_factory_invoke@0x56A280` | 48 |
| iOS arm64 | `motion_EmotePlayer_factory_invoke@0x1001C5F18` | 35 |
| iOS armv7 | `motion_EmotePlayer_factory_invoke@0x1C3158` | 35 |

四端共同状态机：

```text
if memberName != null:
    return MEMBERNOTFOUND
if argc == 1 && arg0.Type == Void:
    return S_OK                  # empty-adaptor sentinel
clear/materialize result frame
if argc < 1:
    return BADPARAMCOUNT
instance = factory(objthis, copy(arg0))
attach instance; ignore surplus args
```

本地 NCBind `paramsFunctorWithInstance` 把 Factory 的第一个 C++ formal 当作隐式 receiver，
不计入脚本 `argc`。原本只有 `tTJSVariant` 一个 formal，因此脚本可见参数数被算成零；修复
后的共同源码签名是：

```cpp
static EmotePlayer *factory(iTJSDispatch2 *, tTJSVariant rmDispatch);
```

第一个参数只恢复 wrapper 的隐式 receiver 槽，native body 仍只消费 arg0 的 Variant，未
增加字段、owner 或额外对象。

## 4. Boolean typed setter 与 `selectorEnabled` 前置状态

### 4.1 typed Boolean wrapper

| 目标 | typed bool invoke | 完整指令数 | 真值转换 |
|---|---:|---:|---|
| Android arm64-v8a | `sub_68C974@0x68C974` | 68 | wrapper 内联完整 type switch |
| Android armv7 | `sub_56CC48@0x56CC48` | 48 | `sub_496CC4@0x496CC4` |
| iOS arm64 | `sub_1001C931C@0x1001C931C` | 51 | `sub_100037640@0x100037640` |
| iOS armv7 | `sub_1C6F78@0x1C6F78` | 73 | `sub_3589C@0x3589C` |

共同伪代码：

```text
if argc < 1:
    copied = Void
else:
    copied = copy(arg0)
converted = copied.operator bool()
destroy(copied)
invoke(nativeInstance, converted)
```

与 `drawvisible` 不同，这里的通用 `CastCopy` 接收 `const tTJSVariant&`；non-const Object
conversion 不参加候选，现有 NCBind 已正确落到 const `operator bool()`。定向运行证实
`false`、`false`、`Void` 三个输入均正常通过，因此无需新增全局 converter。

### 4.2 native trigger leaf 与 selector sync

本轮 fresh decompile/disassembly 的 `debugPrint` setter 四端均只有 3 条指令：

| 目标 | setter leaf | 行为 |
|---|---:|---|
| Android arm64-v8a | `0x67F338` | 忽略 bool，固定写 `1` |
| Android armv7 | `0x5620EE` | 忽略 bool，固定写 `1` |
| iOS arm64 | `0x1001B61D4` | 忽略 bool，固定写 `1` |
| iOS armv7 | `0x1B5FC6` | 忽略 bool，固定写 `1` |

`queuing`、`directEdit` 同构。`selectorEnabled` 还会无条件进入 selector sync；其四端完整
数据流已记录在
`analysis/motionplayer_emoteplayer_scale_trigger_variablekeys_animating_four_binary_2026-08-27.md`：

```text
fresh = new Array
variableLabelsBase = fresh
fresh.Items = native(variableLabels).Items
dirty = true
... selector deque synchronization ...
```

构造函数故意让 `variableLabels` / `variableLabelsBase` 在第一次 metadata reset 前保持
Void。因此，直接在全新 Engine 上触发 selector sync 会在 `native(variableLabels)` 处按
参考边界抛异常；这不是 Boolean conversion 失败，也不应通过产品 null fallback 修补。
原单测缺少真实前置状态，现已在触发 property 前调用 `resetMetadataState()`，建立 metadata
路径本来就会建立的三个 Array/Dictionary owner。

## 5. 本地改动对照

1. `cpp/plugins/DrawDeviceD3D.cpp`：仅把 ambiguous
   `static_cast<bool>(value)` 改为显式 `value.operator bool()`；PropGet、status 特判、Variant
   生命周期均不变。
2. `cpp/plugins/motionplayer/EmotePlayer.h/.cpp`：Factory 增加无名
   `iTJSDispatch2 *` receiver formal；native allocation 仍只以 `rmDispatch` 构造单一
   Engine-sized payload。
3. `tests/unit-tests/plugins/motionplayer-dll.cpp`：Boolean trigger fixture 在 selector sync 前
   走 `resetMetadataState()`；产品 `syncSelectorControls_guess()` 不增加任何 guard 或 fallback。
4. `cpp/plugins/DrawDeviceD3DIntf.h/.cpp` 增加一个未注册、默认关闭的 differential-test render
   manager override；它只让 headless native 用例注入内存 texture/无操作 composite manager，传入
   null 会恢复原有缓存 named `"opengl"` manager 的产品路径，不改变脚本表面或正常运行行为。
5. 曾用于诊断的自定义全局 Boolean converter 已撤回；现有通用 NCBind source structure
   保持不变。

## 6. 最终验证

定向回归全部通过：

- Boolean property：1 case / 61 assertions；
- `*typed Factory*`：2 cases / 148 assertions；
- DrawDeviceD3D 七类脚本表面：1 case / 456 assertions。

DrawDeviceD3D 定向用例在 `drawvisible` 修复后曾于后半段崩溃。macOS crash report 的真实栈是
`glGetIntegerv -> TVPRenderManager_OpenGL::InitGL -> GetD3DRenderManager -> capture`，说明用例在
headless 进程里错误进入了 live OpenGL 初始化，而不是 `AssignTexture` 或 Variant 生命周期错误。
加入上述未注册 test seam 后，用例使用 exact-size 内存 texture，并保留产品默认 manager 路径；
逻辑 Layer 尺寸断言也改为 native `GetWidth()/GetHeight()`，不再把软件 backing bitmap padding
误当脚本可见尺寸。

最终、完整重链接后的结果：

```text
motionplayer-dll:
  test cases: 357 | 356 passed | 1 expected headless-OpenGL skip
  declared-order assertions: 23259 | 23259 passed
  random seed 2862347432 assertions: 23260 | 23260 passed

motionplayer-ttstr-hash-test:
  test cases: 23 | 23 passed
  assertions: 150 | 150 passed
```

`git diff --check` 返回零。对 `cpp/plugins/motionplayer/` 与 `DrawDeviceD3D.cpp` 的 recovery-only
诊断扫描得到 PRTDIAG/stdout/stderr/platform debug calls 全部为零；六个 `TVPAddLog` 均为参考行为
或未修改的既有 error path。由此，本报告复核的三项边界、MP-V07 与 MP-V08 均已闭合。
