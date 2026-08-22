# Motion.Player 误拷贝 SourceCache `loadSource/clearCache` façade 四端清理（2026-08-16）

> **V246 更正（2026-08-18）**：本文删除 dead `clearCache` façade的结论继续成立，但当时把
> `_lastCanvas` 当成独立 native draw owner 是错误的。四端 Canvas 尾与 Player ctor/dtor 已证明
> 该 Variant member/publication 不存在；相关表述已在下文就地修正。

## 结论

本地 Player 的以下两项没有当前四参考来源：

```cpp
void Player::loadSource(ttstr name);
void Player::clearCache();
```

真正的 script-visible `loadSource` / `clearCache` 属于 SourceCache，并由 ResourceManager
重新注册相同 base callbacks。Player 内部 render-source fallback 也会动态调用
`sourceCache.loadSource(source, descriptor)`，但 receiver 是 Player 持有的 SourceCache TJS
对象，不是 Player 自己。

四端共同证据：

- 每端 `loadSource` 与 `clearCache` 各只有一个 UTF-16LE literal；
- `clearCache` 的全部 code xref 只属于 SourceCache/ResourceManager registrar；
- `loadSource` 的全部 code xref 只属于这两个 registrar 与
  `Player_resolveRenderSource_guess` 的 SourceCache receiver call；
- 完整 92-member Player registrar 没有两名，recovery function-name 查询也没有
  Player 同名函数；
- 本地 Player::loadSource 零 caller，它唯一调用的 `SourceCache::loadSourceByName` 也无其他
  caller，且注释已经承认它只是 Web compatibility boundary；
- 本地 Player::clearCache 只有一处宽泛 smoke test 调用，并额外清空 `_lastCanvas`，把两条
  native 中独立的 owner 生命周期错误耦合。

本轮删除两个 Player façade、无来源 `SourceCache::loadSourceByName` 和 smoke 死调用。
真实 SourceCache/ResourceManager 注册面、descriptor-keyed list cache与Player render-source
resolver的生命周期均保持不变；不存在Player `_lastCanvas` owner。

## 四端函数映射

| 目标 | Player registrar | SourceCache registrar | ResourceManager registrar | Player render-source resolver |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D3DA8` | `0x6A5988` | `0x6A8C9C` | `0x6BEF50` |
| Android armv7 | `0x597EC8` | `0x57B0DC` | `0x57C3A8` | `0x58AD94` |
| iOS arm64 | `0x1001244F8` | `0x100100F90` | `0x100102E88` | `0x1001143E0` |
| iOS armv7 | `0x123848` | `0xFE12A` | `0x1002FC` | `0x111E08` |

## 唯一 UTF-16LE literal 与完整 xref

### Android arm64

| name | literal | 全部 xref |
|---|---:|---|
| `loadSource` | `0x14D583A` | SourceCache registrar `0x6A5A74`; ResourceManager registrar `0x6A8D88`; Player resolver `0x6BF030`, `0x6BF038` |
| `clearCache` | `0x14D5850` | SourceCache registrar `0x6A5AE4`; ResourceManager registrar `0x6A8DF8` |

### Android armv7

| name | literal | 全部 code xref |
|---|---:|---|
| `loadSource` | `0xD853A6` | SourceCache registrar `0x57B0EC`, `0x57B0F4`; ResourceManager registrar `0x57C3B8`, `0x57C3C0`; Player resolver `0x58AE00`, `0x58AE06` |
| `clearCache` | `0xD853BC` | SourceCache registrar `0x57B102`, `0x57B10A`; ResourceManager registrar `0x57C3CE`, `0x57C3D6` |

相邻无函数归属 data xref 分别是各短 registrar/resolver 的 literal pool：loadSource
`0x57B134/0x57C4A0/0x58B0B4`，clearCache `0x57B13C/0x57C4A8`。

### iOS arm64

| name | literal | 全部 xref |
|---|---:|---|
| `loadSource` | `0x10195BC98` | SourceCache registrar `0x100100FA8`; ResourceManager registrar `0x100102EA0`; Player resolver `0x100114494` |
| `clearCache` | `0x10195BCAE` | SourceCache registrar `0x100100FC8`; ResourceManager registrar `0x100102EC0` |

### iOS armv7

| name | literal | 全部 xref |
|---|---:|---|
| `loadSource` | `0x174DFFC` | SourceCache registrar `0xFE136/0xFE13C/0xFE148`; ResourceManager registrar `0x100308/0x10030E/0x10031A`; Player resolver `0x111ED4/0x111EDC` |
| `clearCache` | `0x174E012` | SourceCache registrar `0xFE158/0xFE15E/0xFE16A`; ResourceManager registrar `0x10032A/0x100330/0x10033C` |

四端都没有 Player registrar xref。

## Receiver 与参数边界

`Player_resolveRenderSource_guess` 的共同 fallback 是：

```text
sourceCache = Player.persistentSourceCacheObject
source      = owning copy of current raw source
descriptor  = owning copy of Player.persistentSourceDescriptor
result      = sourceCache.FuncCall("loadSource", source, descriptor)
release descriptor/source/sourceCache owners in native order
```

这里是两个 Variant 参数的 descriptor-keyed SourceCache API。旧本地
`Player::loadSource(name)` 则只传一个名字，再进入：

```cpp
SourceCache::loadSourceByName(Player *, name, currentSource)
```

后者不命中 descriptor/list cache，而是直接走 raw source resolution；它不是同一 ABI、同一
cache topology 或同一 receiver。

SourceCache 与 ResourceManager 注册同名 callbacks 也不表示 Player 继承这些方法：
ResourceManager 源级继承 SourceCache，并在自己的 registrar 中重发 base rows；Player 只是
持有独立 Variant/native pointers。

## `clearCache` 生命周期差异

真实 SourceCache::clearCache：

- 逐项 Invalidates cached Layer；
- 按 list node owner 释放 descriptor-backed cached Layers；
- 保留 constructor-owned scratch/buf Layer；
- 不存在可供它接触的 Player `_lastCanvas` Variant。

旧 Player wrapper 在转发 SourceCache clear 后又 `_lastCanvas.Clear()`。四端没有这个复合
入口，V246 进一步证明 draw/capture 路径也没有该字段的提交、替换或析构。因此删除 wrapper
与整个伪 Variant 字段才是更接近 native 的对象结构。

## 修正与验证

- 删除 Player `loadSource/clearCache` declaration/body；
- 删除 `SourceCache::loadSourceByName` declaration/body；
- 删除 draw-cache smoke 对 Player::clearCache 的死调用；
- Player adaptor absence 回归加入两名，并保持失败 result 不变；
- 保留 SourceCache/ResourceManager 的 `loadSource/clearCache`、Player
  `resolveRenderSource_guess` 与真实 `findSource` internal bridge；
- 四份 recovery IDB 的四类函数已写入 receiver/ownership 注释、强制回读并原位保存；
- Player façade与 `loadSourceByName` 零匹配，SourceCache `clearCache` 与 typed loadSource
  仍存在；
- motionplayer 测试 TU Emscripten syntax-only 通过；
- `Web Debug Build` 最终链接通过；
- 限定 `git diff --check` 无新增内容级 whitespace error，仅有既有 CRLF 提示。
