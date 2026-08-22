# MotionPlayer `playing` / `allplaying` 四端恢复（2026-08-14）

## 结论

四份参考二进制把两个只读 Boolean 属性实现为两种不同查询：

```cpp
bool Player::getPlaying() const {
    return playing;
}

bool Player::getAllplaying() const {
    for(std::size_t i = 1; i < nodes.size(); ++i) {
        MotionNode &node = nodes[i];
        if(node.nodeType != 3) continue;
        Player *child = unwrapPlayer(node.childPlayerVar);
        if(child->getAllplaying()) return true;
    }
    return playing;
}
```

`playing` 只读取本 Player 的播放字节。`allplaying` 则先遍历 type-3 nested-motion
子播放器并递归查询，只有没有任何子树返回 true 时才读取本地字节。它不遍历合成根节点，
也不遍历 type-4 particle 子播放器。

当前端口修改前用 range-for 从根节点开始，并对所有 nodeType 无条件调用
`getChildPlayer()`；这会让默认根节点的 Void Variant 在一个本应为纯本地查询的路径上抛出。
本轮已恢复四端一致的索引和 type gate。

## 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| local `playing` getter | `0x6D6B74` | `0x598FEA` | `0x1001256C8` | `0x1248EC` |
| recursive `allplaying` getter | `0x6CA214` | `0x5924EC` | `0x10011CEA4` | `0x11B8C4` |
| `playing` registration site | `0x6D4F60` | `0x598328` | `0x100124B68` | `0x123E34` |
| `allplaying` registration site | `0x6D4FC4` | `0x598342` | `0x100124B90` | `0x123E56` |

两个属性在四端都通过 typed Boolean property helper 注册，setter member pointer 为空。
Android arm64 registration 先为 `playing` 构造 getter object，再注册字符串；随后同样为
`allplaying` 构造 getter object。其他三端的反编译直接显示对应 helper 调用。

## 本地 playing 字段

local getter 都只有一次 unsigned-byte load 和 return：

| 目标 | playing 字段 |
| --- | ---: |
| Android arm64 | `Player+1099` |
| Android armv7 | `Player+751` |
| iOS arm64 | `Player+987` |
| iOS armv7 | `Player+687` |

这些偏移与 `playImpl` 的成功提交、chara live writer 的失效写入和 `stop` 的唯一写入完全
一致。因此源码成员虽然沿用 `_allplaying` 旧名，实际它是单个 Player 的 local playing byte；
聚合语义只存在于 recursive getter 中，不是第二个持久字段。

## deque 容器与循环边界

四端的容器 ABI 不同，但源级循环一致：

- 初始索引固定为 `1`，所以 index 0 的 constructor-created synthetic root 永远不读取；
- 循环条件是 `index < current size`；少于两个节点时直接回退 local byte；
- 每次递归 child 返回 false 后重新读取父 Player 的 deque size；
- node address 由当前 index 经各平台 deque block-map 算址；
- 只有 `nodeType == 3` 才读取 child Variant；type 0/1/2/4 等直接前进；
- 第一棵返回 true 的 child subtree 立即短路，父 local byte不读取；
- 所有 eligible child 都为 false 后才把 local byte规范化为 Boolean。

Android arm64/armv7 是 libc++ deque 的分块首尾迭代器布局；iOS arm64/armv7 使用
`map + logical-start + size` 布局。对应 MotionNode stride 分别为 `2632 / 2272 / 2648 /
2228` 字节。这些实现差异不会改变上述索引、重读 size 或 nodeType gate。

在正常 motion tree 中，getter 不运行脚本 callback，因此递归过程中改变父 deque 的常规路径
并不明显；不过四端代码生成确实在 child 返回后重新读取 size。本地实现用显式 index 和
`_nodes.size()` 循环条件保留了该边界，而不是缓存 range-for 的 end iterator。

## child Variant 解包与对象生命周期

type-3 节点的 `childPlayerVar` 是持有 TJS dispatch 的 owning Variant。聚合 getter只借用其
dispatch，不 AddRef/Release，也不转移所有权：

1. Variant 不是 object 类型时先走普通 Variant object conversion；不可转换值抛异常；
2. object dispatch 为空时 native pointer 记为 null；
3. 对非空 dispatch 用 Player 的全局 NCB class ID 查询 native instance；
4. HRESULT 失败、native adaptor 为空或对象属于错误 native class时 pointer 记为 null；
5. getter仍无条件递归调用该 pointer。

第 5 步是四端一致的不安全边界。反编译 CFG 都把失败分支汇合到
`getAllplaying(nullptr)`，递归函数随后立即读取 Player 容器/字段，所以 malformed type-3
节点会空指针崩溃。端口的 `MotionNode::getChildPlayer()` 已保留“非 object 抛出；null 或
wrong-native 返回 nullptr”，本轮在调用点删除了旧 null guard，从而不把原版 crash 静默
改成 skip。该 crash 不做进程内单元测试，只以四端 CFG 与源码无 guard 记录锁定。

递归借用关系不会延长 child 生命周期；有效 motion tree 必须由 node Variant 持有 adaptor，
adaptor 再持有 native Player。父 Player/node 析构时 Variant release adaptor，才结束这条
ownership 链。getter自身不建立临时 owner。

## 查询顺序的可观察结果

- parent local=false、任意 nested child subtree=true：`playing=false`，`allplaying=true`；
- parent local=true、所有 child=false：两个属性均为 true，但 `allplaying` 仍先遍历全部
  eligible child；
- parent local=true、某个 type-3 child Variant不可转换：`allplaying` 仍抛异常，不会因
  local true 提前返回；
- synthetic root 的 nodeType/child Variant 不影响结果；
- type-4 particle 节点即使含 malformed `childPlayerVar` 也不读取该槽；
- `stop()` 只清 parent local byte；仍播放的 type-3 descendant 可让 parent
  `allplaying` 保持 true。

最后一条说明 `stop` 是 local playback gate，不是递归停止或 subtree teardown。

## 本地修正与回归

- 删除两个过时的单目标绝对地址注释；
- `getPlaying()` 保持只读 `_allplaying`；
- `getAllplaying()` 改为从 index 1 开始的 size-reloading index loop；
- 增加 `nodeType == 3` gate，明确排除 root 与 particle children；
- type-3 解包后无 null guard 递归，保留 reference malformed-tree boundary；
- 保留 optional diagnostics，但它只在 child 返回 true或回退 local 时执行。

新增回归覆盖：

- root 即使标成 type 3 且 child Variant 为 integer也不读取；
- type-4 节点的坏 child Variant 不参与聚合；
- child local=true 时 parent 的 `playing=false`、`allplaying=true`；
- parent local=true 且 child=false 时最终回退 local true；
- parent local=true 也不会跳过 type-3 Variant conversion，坏 Variant仍抛异常。

## IDB 改进

四份 recovery IDB 均已：

- 命名 `Player_getPlaying_guess` 与 `Player_getAllplaying_guess`；
- 应用 `bool(void *self)` 函数类型；
- 在函数入口记录 local/recursive、index range、size reload、type gate 与 malformed boundary；
- 在两个 registration site 记录只读 typed Boolean 属性和 null setter；
- 保存到各自 recovery IDB。

## 验证

- `Web Debug Build` 完整重编 `PlayerCore.cpp`、静态库并成功链接 WebAssembly；
- 聚合 `motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten defines/includes/ABI 参数执行
  `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss` literal-operator 弃用 warning；
- 相关源码、回归与分析文档的 `git diff --check` 通过；只有工作树既有 LF/CRLF 提示。
