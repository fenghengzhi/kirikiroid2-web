# Motion.Player `useD3D` 四参考二进制审计（2026-08-13）

## 结论

`useD3D` 是 Player 自身的可读写 Boolean 属性，不是仅供 Web 端选择
renderer 的临时开关。四个当前参考二进制均保留一个构造默认 `false` 的 byte：

1. NCB setter 直接写该 byte，getter 直接读取；
2. `draw(D3DAdaptor)` 成功识别目标后，在进入直接 D3D render 前强制写
   `true`；
3. 普通 Layer、SeparateLayerAdaptor、无目标及失败路径均不把它写回
   `false`，因此该状态具有粘滞性；
4. 普通 draw 完成 render-item prepare 后，以该 byte 选择 shared-D3D 或
   canvas 路径；
5. source-spec 1 的 source miss 仅在该 byte 为 true 时尝试 KRKR atlas
   helper；helper 失败仍走通用 `findSource(src/icon)` fallback。

完整字段访问扫描每端恰好得到六个点：ctor、getter、setter、draw 写、draw
读和 source consumer。没有隐藏的 child-copy、析构写或 per-frame reset。

## 注册、accessor、布局与默认值

普通字符串索引找不到 `useD3D`；UTF-16LE 原始字节搜索在每端唯一命中，
并回到 `Player_ncb_registerMembers_guess`。

| 目标 | UTF-16 | 注册 | getter | setter | 字段 | ctor 清零 |
|---|---|---|---|---|---:|---|
| Android arm64 | `0x14D65E8` | `0x6D5A38` | `0x6931C0` | `0x6D6D00` | `+909` (`0x38D`) | `0x6CC2E8` |
| Android armv7 | `0xD85EE6` | `0x5985C4` | `0x570F4C` | `0x59909E` | `+629` (`0x275`) | `0x5936DE` |
| iOS arm64 | `0x10195CD74` | `0x100124F5C` | `0x1000F4090` | `0x100125878` | `+797` (`0x31D`) | `0x10011ED08` |
| iOS armv7 | `0x174F0D8` | `0x1241C4` | `0xF0BDC` | `0x124ACC` | `+565` (`0x235`) | `0x11D66A` |

Android arm64 setter 显式执行 `value & 1`；另外三端的 setter body 是单
byte store，由 typed NCB Boolean adapter 完成入参转换。脚本整数 `0` 写
false，非零（包括负数）写 true。

## draw 数据流与粘滞边界

| 目标 | D3DAdaptor 命中后写 true | prepare 后读取 |
|---|---|---|
| Android arm64 | `0x6D3420` | `0x6D34C8` |
| Android armv7 | `0x5978AA` | `0x597920` |
| iOS arm64 | `0x100123D08` | `0x100123DB0` |
| iOS armv7 | `0x122FC0` | `0x12306C` |

四端控制流相同：

```text
draw(target)
  if target is D3DAdaptor and native pointer != null:
      player.useD3D = true
      renderToD3DAdaptor(target)
      return
  if target is SeparateLayerAdaptor and native target != null:
      renderToSeparateLayerAdaptor(target)
      return
  prepareRenderItems()
  if prepare succeeded and player.useD3D:
      render through process-shared D3DAdaptor
  else:
      use ordinary canvas route
```

关键边界是这些分支没有对称的 `useD3D = false`。因此一次显式
`draw(D3DAdaptor)` 会改变后续普通 draw 和 source load 的路径；要恢复普通
模式必须通过公开 `useD3D=false` 属性写入（或销毁并重建 Player）。

## source consumer

| 目标 | useD3D gate | atlas helper |
|---|---|---|
| Android arm64 | `0x691FB0` | `0x6931C8` |
| Android armv7 | `0x570650` | `0x570F54` |
| iOS arm64 | `0x1000F333C` | `0x1000F4098` |
| iOS armv7 | `0xEFB7C` | `0xF0BE4` |

该 gate 位于 source-spec 1 的 module/source miss 分支。先保留 raw `src`
owner；useD3D 为 true 时尝试 atlas helper。helper 成功会提交 texture、尺寸、
origin、rect/clip 等缓存数据并返回；false 或 helper 失败时，都继续清空
texture、构造 `src + "/" + icon`（空 src 仍可产生前导斜杠），再调用通用
`findSource`。source-spec 2 的 Win atlas 路径不受 useD3D 控制。

## 本轮落地

- 清除 `Player.h` 和 draw/source 路径中旧 `libkrkr2.so` 单端地址断言，改写
  为四端共同语义；
- 保留现有直接 Boolean getter/setter、D3D target 强制 true、shared-D3D
  gate 与 source atlas gate；
- 增加默认值、NCB 0/1/负数转换、D3D target 强制 true、空目标不清零、
  显式 setter 清零的回归测试；
- 四个 IDB 各命名 getter、setter、atlas helper，共 12 个函数；标注注册、
  ctor、draw 写/读和 source gate，并保存数据库。

## 验证

- Web Debug 全量重编与最终链接通过；随后复跑为
  `ninja: no work to do.`；
- Wasmtime Headless 全量重编与最终链接通过；随后复跑为
  `ninja: no work to do.`；
- 以 Web Debug 的实际编译参数对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 Emscripten
  `-fsyntax-only`：通过，仅有仓库既有的 `_tss` deprecated warning；
- `git diff --check`：通过，仅报告工作树既有的 LF/CRLF 转换 warning。
