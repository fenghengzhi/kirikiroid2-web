# D3DEmotePlayer shell-only visible、always-true listener 与无渲染 consumer 四参考审计

## 结论

四份当前参考共同证明 `D3DEmotePlayer` 的 `show`、`hide` 与 `visible` property 是一组
只读写 shell 尾部 Boolean byte 的兼容表面：

```text
show()       -> shell.visible = true
hide()       -> shell.visible = false
get visible  -> return shell.visible
set visible  -> shell.visible = converted Boolean
```

四个 body 都没有读取 primary/secondary `EmoteObject *`，没有进入 `EmoteEngine` 或
embedded `Player`，也不写 Player root dirty/visibility。它们因此在 factory 刚创建但尚未
`load` 的 shell 上合法，在 `clear` 后仍合法，而且 `clear` 不重置该 byte。

更反直觉的是，D3D listener virtual `IsVisible()` 也不读取这个 byte。它只在 owner scale
改变时把 `baseScale * userScale` 提交给 Engine scale controller，最后无条件返回 true。
相邻 listener `Draw()` 同样直接 transform `(0,0)` 并进入 Player texture renderer，完全
不读 shell visible。当前四参考版本中，该 byte 的唯一 live read 是公开 property getter；
它可以被脚本观察，却不控制 listener draw。

本地旧实现把 `show`、`hide` 和 setter 额外转发到 `Player::setVisible`，同时改变 Player
root visibility/dirty，并在未 load 时解引用 null primary。这一行为不在四端原生目标中，
现已删除。

## 四端目标映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `show` shell leaf | `0x530448` | `0x494A0C` | `0x100232E28` | `0x231A98` |
| `hide` shell leaf | `0x530454` | `0x494A14` | `0x100232E34` | `0x231AA0` |
| `visible` getter | `0x53045C` | `0x494A1C` | `0x100232E3C` | `0x231AA8` |
| `visible` setter | `0x530464` | `0x494A22` | `0x100232E44` | `0x231AAE` |
| listener `IsVisible` | `0x53409C` | `0x4978BC` | `0x1002363E0` | `0x2350C2` |
| listener `Draw` | `0x53412C` | `0x497930` | `0x100236448` | `0x23511A` |

shell 字段偏移与已有 constructor/layout 审计一致：

| ABI | `primary` | `secondary` | `baseScale` | `userScale` | `visible` | `smoothing` |
|---|---:|---:|---:|---:|---:|---:|
| 64-bit | `+0x18` | `+0x20` | `+0x28` | `+0x2C` | `+0x30` | `+0x31` |
| 32-bit | `+0x10` | `+0x14` | `+0x18` | `+0x1C` | `+0x20` | `+0x21` |

四个属性 body 在 64 位只访问 byte `+48`，32 位只访问 byte `+32`。AArch64 setter
机器码显示 `a2 & 1`，另三端在正确 Boolean prototype 下显示直接 byte store；差异来自
calling convention/优化，typed NCBind converter 对外都先得到规范 Boolean。

## Registrar 与 descriptor 证据

| member | descriptor | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 5 `show` | typed no-argument `void` method | `0x52EB64` | `0x494140` | `0x100232390` | `0x231042` |
| 6 `hide` | typed no-argument `void` method | `0x52EB80` | `0x494156` | `0x1002323B0` | `0x231060` |
| 7 `visible` | typed Boolean read/write property | `0x52EBA8` / `0x52EBDC` | `0x494174` | `0x1002323DC` | `0x23108A` |

所有 stored member adjustment 都为 0。#5/#6 复用普通 zero-argument typed method
factory，不是 raw callback；#7 与 `smoothing` 共用 Boolean property family，但 getter/
setter 指向各自 shell byte。generated adapter 仍保留标准 membername/receiver/result-clear/
minimum-argc/native-unbox 顺序，surplus 参数忽略；native leaf 本身不重复做 receiver 检查。

## listener 数据流

四端 `IsVisible()` 的共同源码级行为为：

```text
ownerScale = owner.scaleX
if baseScale != ownerScale:
    baseScale = ownerScale
    finalScale = ownerScale * userScale       // float multiply
    engine = primary->engine                  // only on changed-scale path
    engine.dirty = true
    scaleController.setTarget(
        &finalScale, duration=0, power=1, append=engine.queuing)
return true
```

可见边界：

- owner scale 未变时，即使 primary 为 null，函数也不解引用它并返回 true；
- owner scale 变化而 primary 为 null 时，仍保留 unchecked dereference/crash 前置条件；
- `visible=false` 不跳过 scale synchronization，也不改变返回值；
- float 比较使用普通 `!=`，所以 NaN owner scale 每次都走 changed 分支；
- shell byte 与 Player root visibility 完全分离，二者可以同时保存相反值。

D3DLayer listener fan-out 使用此 virtual 返回值决定是否调用 `Draw`。由于返回值固定 true，
shell visibility 不参与 fan-out gate。`Draw` 的四端共同 body 只取 owner、primary 和 target
texture：先 virtual transform 零原点，再取得 embedded Player 并调用 direct-texture
renderer；不存在隐藏 visible read。

## 生命周期与异常边界

- constructor 把 `visible`、`smoothing` 都初始化为 false；
- `clear` 只按 secondary→primary 顺序删除两个 raw owner，最后同时清双槽，不写这两个
  shell byte；
- `load` 先 clear，所以已有 visible 值跨 reload 保留；
- `clone(targetOwner)` 构造全新 shell，因而 clone 的 visible 回到 false，不从 source copy；
- show/hide/property assignment 只有单 byte store，不分配、不抛出 motion object 异常，
  也没有“先写 shell、再因 Player 路径失败”的部分提交；
- script property/method wrapper 在到达 leaf 前仍可能因 receiver/native adaptor/Variant
  转换失败，失败时 leaf 未执行。

## 本地源码修正与回归

- `D3DEmotePlayer::setVisible` 删除 `player().setVisible(v)`；
- `show` / `hide` 删除两个 Player root forwarding call；
- header 与 listener 注释明确 shell byte 是 dormant compatibility state，listener 固定 true；
- 真实 D3DEmotePlayer adaptor 回归覆盖未 load 时的 `visible` PropSet/PropGet、zero-arg
  `show`/`hide` FuncCall、Void result、clear 后状态保留；
- loaded shell 回归人为让 inner Player visibility 与 shell byte相反，确认 show/hide/setter
  均不改变 Player root。

## IDB 回写

四份 recovery IDB 已完成：

- 20 个匿名 target 统一命名为 `D3DEmotePlayer_show_guess`、
  `D3DEmotePlayer_hide_guess`、`D3DEmotePlayer_getVisible_guess`、
  `D3DEmotePlayer_setVisible_guess` 与 `D3DEmotePlayer_IsVisible_guess`；
- 20 个 target 应用真实 void/Boolean prototype；
- 12 个 registrar anchor 与 20 个 target 追加 shell-only、always-true 和无 Player
  forwarding 注释；
- 每库新增三个 bookmark，并对 registrar 与五个目标强制反编译回读。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 通过；唯一
  诊断是仓库既存 `_tss` literal-operator whitespace warning；
- 强制刷新 motionplayer archive 时间戳后，`cmake --build --preset "Web Debug Build"`
  重新执行最终 link，成功生成 `index.html/index.js/index.wasm`；
- 定向源码检查确认 D3D `setVisible/show/hide` body 不再包含 `player().setVisible`，而
  Player 自身公开 setter 保持不变；
- 当前构建树没有可直接运行的 native motionplayer Catch2 executable，因此新增 adaptor
  断言已由完整测试翻译单元编译检查，但本轮未单独运行。

