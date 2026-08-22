# Motion.Player 死 `emoteEdit` façade / `_tags` 占位 owner 四端清理（2026-08-16）

## 结论

本地 `Player::emoteEdit(tTJSVariant)` 与其只写不读的 `_tags` 字段没有当前四个参考
二进制的来源依据。它们来自早期单目标 API 猜测，并不是节点 `emoteEdit` 数据流，也不是
只读 `Motion.Player.tags` 属性的实现：

- 四端完整 `Motion.Player` registrar 都没有 `emoteEdit` member；
- 四端各自唯一的 UTF-16LE `emoteEdit` literal，其全部 code xref 只属于
  `Player_initNodeFields_guess`，用于读取每个 layer 的 `emoteEdit` 对象；
- 四端 `Player_getTags_guess` 都 CopyRef Player 中已经提交的 `motion["tag"]` owner；
- 四端 Player 析构尾部都只有 `resourceManager -> motion context -> outline -> meshline ->
  tags` 五个连续 Variant owner 的逆序释放，没有为本地 `_tags = args` 数据流留下第二个
  tag/edit owner；
- 本地 `Player::emoteEdit` 除声明和定义外零调用、零注册，`_tags` 除该死写入外零读者。

因此本轮删除整个未注册 façade 以及孤立 `_tags`。真正的 tag frame owner
`_tagFrameSourceVariant`、节点级 `MotionNode::emoteEditVariant`、Primary/Engine 的
`directEdit` property 均保持不变。

这里不把“linker 删除后不存在”扩大解释为原始源码绝不可能含任意同形 private inline
helper；结论限定为：当前本地 public declaration、out-of-line body、独立 Variant owner
都没有四端可观察来源，而且与已恢复的数据布局冲突。

## 四端映射

| 目标 | Player registrar | `Player_initNodeFields_guess` | `Player_getTags_guess` | Player ctor | Player dtor |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D3DA8` | `0x6B1058` | `0x6D69F8` | `0x6CC110` | `0x6CCEBC` |
| Android armv7 | `0x597EC8` | `0x580FA4` | `0x598E50` | `0x5935C4` | `0x593C24` |
| iOS arm64 | `0x1001244F8` | `0x100108720` | `0x100125544` | `0x10011EC04` | `0x10011F2A0` |
| iOS armv7 | `0x123848` | `0x105E70` | `0x124748` | `0x11D488` | `0x11DCC4` |

## `emoteEdit` 字符串的完整所有权

UTF-16LE byte pattern：

```text
65 00 6D 00 6F 00 74 00 65 00 45 00 64 00 69 00 74 00
```

每端完整搜索都只返回一个 literal：

| 目标 | literal | 全部 xref |
|---|---:|---|
| Android arm64 | `0x14D5D80` | `0x6B10F4`, `0x6B10FC`, `0x6B1134`, `0x6B113C`，全部位于 node-field initializer |
| Android armv7 | `0x581628` | `0x580FE4`, `0x580FFA`，全部位于 node-field initializer |
| iOS arm64 | `0x10195C28E` | `0x100108770`, `0x100108790`，全部位于 node-field initializer |
| iOS armv7 | `0x174E5F2` | `0x105EEA`, `0x105EF0`, `0x105EFC`, `0x105F0E`, `0x105F14`, `0x105F1A`，全部位于 node-field initializer |

共同语义是先用同一 member hint 探测 layer 是否含 `emoteEdit`，存在时 CopyRef 到当前
`MotionNode`，缺失时清空该节点的 retained Variant。这里没有调用 Player method，也没有
写入 Player 级编辑参数缓存。

恢复库 function-name 正则查询同样没有找到 `Player_*emoteEdit*`；仅命中各端独立的
`EmotePlayer_getDirectEdit_guess` / `EmotePlayer_setDirectEdit_guess`。后者是一条已经恢复的
Engine/Primary Boolean property 链，不能据此保留本地 `Player::emoteEdit(args)`。

## 唯一 Player `tags` owner

四端 getter 共同伪代码为：

```cpp
tTJSVariant Player::getTags() const {
    return persistentMotionTagVariant; // tTJSVariant CopyRef
}
```

字段偏移分别是：

| 目标 | tags getter source field |
|---|---:|
| Android arm64 | Player `+1072` |
| Android armv7 | Player `+732` |
| iOS arm64 | Player `+960` |
| iOS armv7 | Player `+668` |

getter 不重新执行 `motion["tag"]` property lookup，不克隆容器，不转换类型，也不修改
Player。普通 motion 初始化把查得的 raw owner 提交到同一字段；forward、rewind、full
reseek 和 `skipToSync` 都消费这一 owner。本地对应字段是 `_tagFrameSourceVariant`。

## 析构槽与额外 owner 的排除

四端析构尾部的连续 Variant 地址为：

| 目标 | resourceManager | motion context | outline | meshline | tags |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `+992` | `+1012` | `+1032` | `+1052` | `+1072` |
| Android armv7 | `+684` | `+696` | `+708` | `+720` | `+732` |
| iOS arm64 | `+880` | `+900` | `+920` | `+940` | `+960` |
| iOS armv7 | `+620` | `+632` | `+644` | `+656` | `+668` |

64 位 Variant stride 为 20 bytes，32 位为 12 bytes。析构严格从 tags 逆序释放到
resourceManager；构造严格建立同一五槽链。假如保留本地 `_tags` 作为一个与真实
motion-tag owner 独立的 `tTJSVariant`，就会要求额外构造/析构槽或另一条可观察 owner
数据流，四端都没有该证据。

## 本地偏差与修正

旧本地路径只有：

```cpp
void Player::emoteEdit(tTJSVariant args) {
    _directEdit = true;
    _tags = args;
}
```

它同时混淆三件不同事：

1. `directEdit` 是 Primary/Engine Boolean property；
2. layer `emoteEdit` 是每节点 retained Dictionary；
3. Player `tags` 是只读 motion tag-frame Array owner。

本轮修正：

- 删除 `Player::emoteEdit(tTJSVariant)` declaration/body；
- 删除零读者 `tTJSVariant _tags`；
- 在真实 Player adaptor absence 回归中加入 `emoteEdit`，要求
  `TJS_MEMBERMUSTEXIST` 查询返回 `TJS_E_MEMBERNOTFOUND`，并保持 result 未修改；
- 保留 `_tagFrameSourceVariant` 的全部初始化、getter、timeline 消费和测试注入路径。

## 验证

- 四库已写入 registrar / node-field initializer / tags getter / dtor 的来源注释，强制
  反编译回读后原位保存；
- `rg` 确认 `_tags`、`Player::emoteEdit` 与 declaration 均为零匹配；
- motionplayer 测试 TU Emscripten syntax-only 通过；
- `Web Debug Build` 最终链接通过；
- 限定 `git diff --check` 无新增内容级 whitespace error，仅有仓库既有 CRLF 提示。

