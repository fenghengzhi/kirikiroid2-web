# MotionPlayer `Player_loadMotion` 调用边界与非播放路径四端对照（2026-08-12）

## 1. 结论

四个当前参考二进制中的 `Player_loadMotion_guess` 都只有两个直接调用语义：

1. `Player_playImpl_guess` 的主播放路径；
2. `Player_initEmoteMotion_guess` 的角度区间切换路径。

draw、直接 D3D texture 绘制、普通 Layer/Canvas 提交、SeparateLayerAdaptor 绘制、progress bridge 和变量读取都不会调用 `findMotion` 或 `Player_loadMotion_guess`。因此，本地旧 `ensureMotionLoaded()` 在这些路径上制造了参考二进制不存在的 ResourceManager 调用、回调、对象构造和状态提交副作用，不能作为“lazy load”兼容行为保留。

## 2. `Player_loadMotion_guess` 与完整直接调用集

| 目标 | load helper | `playImpl` 调用点 | `initEmoteMotion` 调用点 | 直接调用者总数 |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6AE2F0` | `0x6AF710` | `0x6B04E4` | 2 |
| Android armv7 | `0x57F654` | `0x580200` | `0x58099C` | 2 |
| iOS arm64 | `0x1001067BC` | `0x1001075F0` | `0x100107F3C` | 2 |
| iOS armv7 | `0x103BBC` | `0x104BDC` | `0x105578` | 2 |

这里的“2”来自四份 IDB 对 load helper 的完整 code-xref 枚举，不是从本地源码反推。四端都没有第三个 caller，也没有 draw/progress/query 路径经独立 wrapper 进入同一 helper。

## 3. 本轮 fresh decompile 的非播放函数映射

### 3.1 draw、render 与 listener

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `D3DEmotePlayer_Draw_guess` | `0x53412C` | `0x497930` | `0x100236448` | `0x23511A` |
| `Player_drawToTexture_guess` | `0x6D3048` | `0x5976AC` | `0x100123970` | `0x122C10` |
| `Player_draw_guess` | `0x6D3398` | `0x597864` | `0x100123C84` | `0x122F28` |
| `Player_renderToCanvas_guess` | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |
| `Player_renderToSeparateLayerAdaptor_guess` | `0x6D2A38` | `0x597328` | `0x1001233C8` | `0x12257C` |
| `Player_drawToLayerRecursive_guess` | `0x6D0160` | `0x595720` | `0x10012139C` | `0x120168` |

`D3DEmotePlayer_Draw_guess` 四端都先通过 D3D owner 的虚函数变换 `(0, 0)`，再把当前 native texture 和变换后的坐标交给 `Player_drawToTexture_guess`。该调用链没有 motion 查找。

`Player_draw_guess` 的共同高层顺序是：

```text
drawCompat(target):
  if target is D3DAdaptor:
    renderToD3DAdaptor(current state)
    return

  if target is SeparateLayerAdaptor:
    renderToSeparateLayerAdaptor(current state, target)
    return

  prepare current render-item lists
  if d3dDrawMode:
    render through shared D3DAdaptor
  else:
    apply prepared-item translate offsets
    renderToCanvas(target copy, current prepared lists)
    update/capture target state
```

四端伪代码和引用表内都没有 `findMotion`、`onFindMotion` 或 `Player_loadMotion_guess`。普通 Canvas body 很大且因 ABI/优化器呈现差异明显，但其输入一致：现有 Player、目标 Variant、两组 caller-prepared render items；它不是资源解析入口。

### 3.2 progress 与变量查询

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `Player_progressBridge_guess` | `0x6CFE34` | `0x595570` | `0x1001211C0` | `0x11FF88` |
| `EmoteEngine_getVariable_guess` | `0x5341FC` | `0x4979BC` | `0x1001B5D84` | `0x1B5A2C` |

四端 progress bridge 完全归一化为：

```text
progressBridge(player, currentDispatch, frameDt):
  player.currentDispatchRaw = currentDispatch
  frameProgress(frameDt)
  updateLayers()
  calcBoundsRecursive()
  dispatchPendingEvents(currentDispatch)
  player.currentDispatchRaw = null
```

它没有 motion 是否为空的 lazy-load 分支。空状态仍按现有容器和事件状态执行 bridge；ResourceManager 不参与。

四端变量 getter 完全归一化为：

```text
EmoteEngine_getVariable(owner, label):
  player = owner.innerPlayer
  if player.bindScopeList.contains(label):
    return player.readBoundParameterValue(label)
  if player.readDerivedValue(label, out):
    return out
  return player.readBoundParameterValue(label)
```

它只读 Player 已有的 scope/cascade/hash-map 状态，不检查 motion Variant，也不尝试加载。变量容器的生命周期因此独立于某次 draw/query 是否成功解析资源。

## 4. 与旧本地实现的差异

修改前，本地 `ensureMotionLoaded()` 在以下位置被调用：

- `Player::drawCompat`；
- 已于 2026-08-16 删除的 port-side 无参 `Player::draw()`（仅测试调用，参考只有
  one-Variant typed draw）；
- 已于 2026-08-16 删除的 port-side `progressMillisecondsCompat_guess`（它未注册、无
  production caller，且额外钳位毫秒值；测试现显式进入 frame-unit bridge）；
- `drawToD3DImageLike_0x6D5C68`；
- `renderToCanvasLike_0x6C7440`；
- `renderToLayer`；
- `renderToSeparateLayerAdaptor`；
- `Player::getVariable`。

第一次命中会通过 `_stealthChara/_motionKey` 调 ResourceManager `findMotion`，并立即把 result[0]/result[1] 写入 Player。这会带来四端都没有的边界行为：query/draw 可以触发 `onFindMotion`，失败或非 Void 结果会改变后续状态，result owner 的构造/析构时机也被非播放 API 改写。

旧辅助函数名中的 `0x6D5C68`、`0x6C7440` 来自早期 `libkrkr2.so` 分析，既不是当前 Android arm64 参考项目中的函数入口，更不可能跨四 ABI 作为源码身份。它们已分别改成未知精确源码名的语义名：

- `Player::drawToTexture_guess`；
- `Player::renderToCanvas_guess`。

## 5. 本地恢复

本轮源码修改为：

1. 删除两个 `ensureMotionLoaded` overload 及全部八个调用点；
2. 保留 `loadMotionResult_guess`，且它现在只被 `playImpl` 与 `initEmoteMotion` 使用，对应四端各两个直接调用语义；
3. 非播放 draw/render 路径在无现有 motion content 时直接退出，不访问 ResourceManager；
4. 后续 source-structure 复核删除无参 draw 与毫秒 progress 两个未注册测试
   convenience；测试分别进入真实 one-Variant draw 与 frame-unit bridge；
5. Player getter 直接读取既有 HM1/HM2，Engine getter 只读取既有
   scope/HM4/HM1/HM2；两者都不触发 load；
6. 删除本垂直中依赖旧 `libkrkr2.so` 地址身份的注释与 trace 标签。

回归测试 `non-play operations never perform implicit findMotion` 使用记录所有 `findMotion`
路径的假 ResourceManager，分别在全新 Player 上执行真实 one-Variant draw、显式
frame-unit progress bridge 和缺失变量查询，并要求请求列表始终为空。

## 6. IDB 改进

四份 IDB 已写入并保存：

- `D3DEmotePlayer_Draw_guess`；
- `Player_drawToTexture_guess`；
- `Player_renderToCanvas_guess`；
- `EmoteEngine_getVariable_guess`；
- `Player_renderToSeparateLayerAdaptor_guess`。

这些函数均补充了归一化 prototype 和“只消费现有状态、无隐式 load”入口注释。最终 rename/type/comment 后再次 fresh decompile，函数 body 中均无 `findMotion` 或 `Player_loadMotion_guess` 调用；四库保存结果均为 `ok=true`。

## 7. 验证

- Web Debug 完整增量构建与 `index.html`/Wasm 链接成功；
- Wasmtime Headless Debug 的普通 motionplayer 与 guest 对象重编、最终链接成功；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用当前 Web Debug 的真实
  Emscripten 定义和头路径执行 `-fsyntax-only` 成功；
- 诊断仅为仓库既有 `_tss`、imagepacker `nodiscard`、pthread/memory-growth、JSPI
  与 JS library warning；
- `git diff --check` 通过，仅报告工作区既有 LF/CRLF 转换提示；
- 当前环境仍没有可直接运行的 native Catch2 motionplayer 可执行文件，因此新增测试只
  报告为已进入完整翻译单元并通过编译，不声称运行时执行。
