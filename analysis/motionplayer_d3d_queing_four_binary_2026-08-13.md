# D3DEmotePlayer.queing 四参考二进制审计（2026-08-13）

## 1. 结论

`D3DEmotePlayer` 的公开属性名在四份当前参考中均故意拼成 `queing`，不是
`queuing`。它与 `Motion.EmotePlayer.queuing` 使用同一个 `EmoteEngine` 字节，但
两个类的公开拼写不同；`Motion.Player.queuing` 则是内层 Player 的另一字段、另一
状态机，不能混为一谈。

D3D getter 沿 `shell -> primary EmoteObject -> EmoteEngine` 读取原始无符号 byte；
setter 沿同一路径无条件存常量 1。typed ncbind 在进入函数前会把脚本 Variant
转换为 Boolean，但 setter 根本不读取这个参数，因此赋值 false、Void 或 true 都
只会开启 append mode，无法通过该属性恢复 false。

## 2. 注册表、字符串与回调

| 目标 | D3D registrar | `queing` 名称引用 | getter | setter |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x52E8E4` | `0x52ED78` | `0x5304AC` | `0x5304BC` |
| Android armv7 | `0x494078` | `0x4941B4` | `0x494A58` | `0x494A62` |
| iOS arm64 | `0x100232278` | `0x100232438` | `0x100232E84` | `0x100232E94` |
| iOS armv7 | `0x230F46` | `0x2310E2` | `0x231AE4` | `0x231AEE` |

UTF-16LE `queing` 的数据地址分别为 Android arm64 `0x14BE9FE`、Android armv7
`0xD76832`、iOS arm64 `0x10196FD7C`、iOS armv7 `0x1762128`。四个 D3D 表均把
它注册在 `meshDivisionRatio` 与 `hairScale` 之间。四份 IDB 已将八个回调统一命名
为 `D3DEmotePlayer_getQueing_guess` / `D3DEmotePlayer_setQueing_guess`。

## 3. 对象链与字段映射

| 目标 | shell primary slot | EmoteObject Engine slot | Engine byte |
| --- | ---: | ---: | ---: |
| Android arm64 | `+24` | `+8` | `+1161` |
| Android armv7 | `+16` | `+4` | `+593` |
| iOS arm64 | `+24` | `+8` | `+793` |
| iOS armv7 | `+16` | `+4` | `+409` |

共同伪代码为：

```text
getQueing(shell):
    return uint8(shell.primary.engine.appendFlag)

setQueing(shell, convertedBooleanUnused):
    shell.primary.engine.appendFlag = 1
```

两级 pointer load 都没有 null guard。新构造或 `clear()` 后的 D3D shell 没有
primary object；此时直接访问 `queing` 会按原生不变量越过 null，而不是返回 false、
抛自定义 “not loaded” 错误或延迟保存 shell-local 值。本地 private accessor 同样
保持 load-before-use 前置条件。

## 4. 默认值和共享数据流

四个 Engine 构造分别在 Android arm64 `0x67BB3C` 附近、Android armv7
`0x560Bxx` 标量初始化段、iOS arm64 `0x1001B8130`、iOS armv7 `0x1B7976` 将该
字节初始化为 0。故 `load()` 建立 primary object 后，`queing == false`。

同一个 byte 也是 `Motion.EmotePlayer.queuing` 的 backing field；它作为 append/
replace 参数进入 Engine 的 variable controller、timeline、root transform、outer
force 等 writer：false 清除/替换已有工作，true 将新 keyframe 追加到队列。它不
是 Player 的帧游标 `_queuing`：Player 构造默认该字段为 true，而且 Player 的
tick/sync 路径会独立改变它。最小可观察区分为：加载后的 D3D `queing` 为 false，
同一 Engine 内部 Player 的 `queuing` 仍为 true。

## 5. 本地修正与验证范围

实现本身原本已经写入正确 Engine 字节；本轮删除了旧单端绝对地址、旧 commit
叙事和固定偏移行尾注释，改成四端共同的对象链/行为说明。新增回归覆盖：

- load 后 D3D `queing == false`，同时 inner Player `queuing == true`；
- 通过 typed NCB 给 `queing` 写 false 仍得到 true；
- getter 返回 TJS Integer/Boolean 真值；
- D3D 表不存在正确拼写 `queuing`，`TJS_MEMBERMUSTEXIST` 查询返回
  `TJS_E_MEMBERNOTFOUND`。

四份 IDB 同时补入 registrar、getter、setter 和 Engine ctor 的对象链、共享字段、
默认值与 one-way setter 注释。

## 6. 验证

- `cmake --build --preset "Web Debug Build"`：受影响 motionplayer 对象、静态库及
  最终 `index.html/index.wasm` 链接通过；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target
  krkr2_wasmtime_guest`：前台等待达到 64 秒超时，但底层 CMake/Ninja 继续完成；
  等待进程退出后重跑得到 `ninja: no work to do.`；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 真实
  Emscripten 定义、include、ABI 参数与既有 `out/syntax-check` Catch2/test config
  执行 `-fsyntax-only` 成功；唯一诊断为仓库既有 `_tss` literal-operator 弃用
  warning；
- `git diff --check`：通过，仅有工作区 LF/CRLF 转换提示；
- 四份改进后的 IDB 已原位保存。

当前配置没有可直接运行的 `motionplayer-dll` Catch2 executable，因此这里不把完整
测试翻译单元的编译验证表述为运行时测试通过。
