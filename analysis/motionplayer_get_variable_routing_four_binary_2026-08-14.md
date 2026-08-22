# MotionPlayer getVariable：Player/Engine 双层路由四参考复原

日期：2026-08-14

本文只以 `reference/binaries/` 中四个当前参考二进制为证据。它纠正了源码和
recovery IDB 中一个容易隐藏的结构误判：Emote facade 使用的 scope/HM4 路由器
属于 `EmoteEngine`，而 `Motion.Player.getVariable` 直接绑定到 Player 的
HM1/HM2 bound-value reader。旧名
`isLabelInBindScopeListLike_0x6CD16C` 中的地址来自过时目标；该地址在当前 Android
arm64 二进制中落在 Player 析构函数内部，不能继续作为函数身份。

## 1. 四端函数与注册点

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_getVariable_guess` | `0x5341FC` | `0x4979BC` | `0x1001B5D84` | `0x1B5A2C` |
| `Player_hasVariableLabelScope_guess` | `0x6CA54C` | `0x592740` | `0x10011D260` | `0x11BC04` |
| `Player_readSnapshotOrBoundParameterValue_guess` | `0x6CA61C` | `0x592784` | `0x10011D348` | `0x11BC78` |
| `Player_readBoundParameterValue_guess` | `0x6CA77C` | `0x592810` | `0x10011D3D8` | `0x11BD50` |
| D3D shell getter | `0x5309B4` | `0x494D20` | `0x10023315C` | `0x231D70` |

注册表把两套表面明确分开：

- `Motion.Player.getVariable` 的 typed NCB target 是
  `Player_readBoundParameterValue_guess`；Android arm64 和 iOS arm64 的 target
  materialization 分别位于 `0x6D5B50` 和 `0x100124FD8`，32 位对应点在
  `0x59861A`、`0x12423E`。
- `Motion.EmotePlayer.getVariable` 的 target 是
  `EmoteEngine_getVariable_guess`；四端对应 materialization 位于
  `0x67D220`、`0x5613D8`、`0x1001B529C`、`0x1B4F1C`。
- D3D NCB 注册的是独立 shell wrapper。它先沿
  `D3DEmotePlayer -> EmoteObject -> EmoteEngine` 所有权链取得 Engine，再调用同一
  Engine getter；不是另一个 Player 查询算法。

因此旧 IDB 名 `Player_getVariable_guess` 和本地
`EmotePlayer::getVariable -> player().getVariable` 虽然在常见 HM4 为空时经常返回同一
数值，却掩盖了真实类边界和脚本表面差异。

## 2. 精确数据流

四端共同的源码形状为：

```text
Motion.Player.getVariable(label):
  return player.readBoundParameterValue(label)

EmoteEngine.getVariable(label by value):
  player = engine.ownedPlayer
  if player.hasVariableLabelScope(copy(label)):
    return player.readBoundParameterValue(copy(label))
  return player.readSnapshotOrBoundParameterValue(copy(label))

Player.readSnapshotOrBoundParameterValue(label by value):
  if HM4 contains label:
    return HM4[label]
  return player.readBoundParameterValue(copy(label))
```

`Player_readBoundParameterValue_guess` 的 HM1/HM2 规则已在
`motionplayer_parameter_binding_four_binary_2026-08-11.md` 中闭合：可拆分标签只查
HM1 的 joined key 和 `writeVal`，不可拆分标签只查 HM2，miss 返回 `0.0`。

这个路由顺序有三个可观察点：

1. scope deque 命中时完全绕过 HM4，即使 HM4 含有同名键；
2. 非 scope 标签优先返回 HM4，只有 miss 才读 HM1/HM2；
3. `Motion.Player` 脚本 getter 永远不读 HM4。

## 3. 空标签和字符串边界

四个 Engine getter 都在进入 scope helper 前直接复制传入的 `ttstr`，没有
`IsEmpty()`、窄化成 `std::string` 或空字符串早退。空 `ttstr` 是普通键：

- scope deque 可以包含空 `cascadeKey`；
- HM4 可以包含空键；
- HM2 binder 可以写入空键；
- Engine getter 对空键仍执行完整 scope/HM4/bound 顺序；
- Player getter 对空键仍直接查询 HM2。

本地旧实现先执行 `detail::narrow(label)`，再对空结果返回 `0.0`。这会隐藏 HM2/HM4
中合法的空键，并改变 `ttstr` 引用计数与异常清理路径，已删除。

## 4. `ttstr` owner 与调用期生命周期

Engine getter 的入口参数按值拥有一个 `ttstr` 引用。它不会把这个 owner 直接借给
三个 Player helper，而是在每次 helper 调用前建立新的短生命周期副本：

1. copy-retain 后调用 scope scanner；返回后立即 destroy/release；
2. 根据分支，再 copy-retain 后调用 direct-bound 或 snapshot helper；返回后立即
   destroy/release；
3. Engine getter 退出时释放自己的 by-value 参数 owner。

snapshot helper 命中 HM4 时只读取 mapped `double`，不再复制 label；miss 时才建立
第三层短生命周期副本，调用 direct-bound reader，再在返回前销毁。scope scanner
借用 deque 元素的 `cascadeKey`，不 retain 元素 key。

这些副本在正常返回值上通常不可见，但决定低内存异常、栈展开以及最后一个字符串
owner 的释放时机，所以可移植源码保留 by-value helper 边界，没有把它们全部折叠为
`const ttstr &`。

## 5. scope deque 与 HM4 容器实现

### 5.1 variable-track scope deque

scanner 从头到尾比较每个 item 的第一个字段 `cascadeKey`，第一次相等立即返回 true；
走到逻辑 end 返回 false。比较先做 ttstr owner 指针相等快路，再做 null/长度/UTF-16
内容相等。它不排序、不建索引，也不区分 item 的 active slot 状态。

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player 中 deque 锚点 | `+0x520` | `+0x388` | `+0x488` | `+0x330` |
| element stride | `160` | `128` | `160` | `128` |
| deque family | libstdc++ | libstdc++ | libc++ | libc++ |

Android 64 位每块有效容纳三个 160-byte item；Android 32 位每块四个 128-byte
item。iOS libc++ 使用 map + logical start/size，64 位每块 25 个 item，32 位每块
32 个 item。尽管 iterator 机器不同，四端语义都是线性 `[begin,end)` 扫描。

### 5.2 HM4 join snapshot

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player 中 HM4 偏移 | `+0x4D8` | `+0x364` | `+0x458` | `+0x318` |
| hash node 中 mapped double | `+16` | `+16` | `+24` | `+12` |

Android arm64 getter内联 UTF-16 hash 和 bucket 查找；其余目标可见共享 find helper。
实现差异不改变 key 所有权：map node 拥有 `ttstr` key，mapped value 是无引用计数的
raw `double`。命中直接读 mapped slot；miss 不插入、不修改 map。

HM4 仍是 full-reseek 的短生命周期 join snapshot：reset 写入，restore/prune 消费后
清空。Engine getter只是该 snapshot 在存活窗口内的另一个 reader；它不延长 map 或
node 生命周期。

## 6. Engine/Player 对象边界

Engine getter 的 `self` 是 `EmoteEngine`：四端先从以下 owner slot 读取 heap Player
指针，再把该指针传给全部 helper：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `Engine+0x428` | `Engine+0x214` | `Engine+0x2B8` | `Engine+0x15C` |

这些偏移与四端 EmoteEngine 构造/析构中唯一 Player owner 一致。D3D shell 不复制
Player，也不缓存 helper 结果；它借用 owned EmoteObject 中的 Engine，调用结束后只
销毁自己的 by-value label 临时量。`Motion.EmotePlayer` 的 adaptor payload 本身是
Engine-sized 单对象，因此可直接把 facade `this` 作为 Engine self。

## 7. 本地恢复

本轮源码修改：

1. `Player::getVariable` 恢复为 direct bound HM1/HM2 reader；
2. 新增 `EmoteEngine::getVariable`，实现 scope → bound / 非 scope → HM4 → bound；
3. Primary `EmotePlayer` 注册表直接绑定 Engine getter，不保留 facade forwarding body；
   独立 `D3DEmotePlayer` shell wrapper 仍沿 owner 链调用 Engine getter；
4. scope helper 改名为 `Player_hasVariableLabelScope_guess`，删除伪造的旧地址名；
5. 抽出 `Player_readSnapshotOrBoundParameterValue_guess` 对应的 source-level helper；
6. 删除空 label 的窄化与早退；
7. 修正 HM4、D3D getter 和 load-caller 文档中把 Engine router 叫作 Player getter 的
   旧叙述。

回归在一个 Engine-owned Player 上分别钉住：

- 同名 HM2=`1.25`、HM4=`9.5`：Player getter 返回 `1.25`，Engine getter 返回
  `9.5`；
- scope deque 含 label 且 HM2=`2.5`、HM4=`8.5`：Engine getter绕过 snapshot，
  返回 `2.5`；
- 空 label 同时存在 HM2=`3.75`、HM4=`7.75`：Player/Engine 分别返回对应层的值，
  不提前返回零。

## 8. recovery IDB 与验证

四份 recovery IDB 已统一：

- 把旧 `Player_getVariable_guess` 改为 `EmoteEngine_getVariable_guess`；
- 命名 scope scanner 与 snapshot-fallback helper；
- 给四个 getter/helper 应用 `bool`/`double` prototype；
- 在 Player、EmotePlayer、D3D 三组注册点注明不同 target 与对象链；
- 保存四库均返回 `ok=true`。

验证结果：

- `cmake --build --preset "Web Debug Build" --parallel` 完整成功；
- 使用 Web Debug 的真实 Emscripten defines/includes/ABI 参数对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only` 成功；诊断只有
  仓库既有 `_tss` literal-operator warning 和测试配置宏的重复定义 warning；
- targeted `git diff --check` 通过，仅有既有 LF/CRLF 转换提示；
- 当前配置没有可直接运行的 native Catch2 motionplayer 可执行文件，因此这里只声明
  测试翻译单元已完整编译，不把语法检查写成运行时执行。

## 9. 2026-08-15 typed binding 补充

后续对四端 Primary registrar 和 one-ttstr/double NCBind specialization 的 fresh 回读
进一步确认：`Motion.EmotePlayer.getVariable` 直接存储上述 Engine getter 与零 member
adjustment；本地一度保留的 `EmotePlayer::getVariable` forwarding declaration/body 是
不存在于参考二进制的源级层次，现已删除。参数 Variant 到 owned `ttstr`、Engine 调用、
临时 `tvtReal` Variant 返回值交接以及 null result 边界详见
`motionplayer_get_variable_direct_binding_typed_owner_four_binary_2026-08-15.md`。
