# MotionPlayer prepared-item type-4 particle 递归四端复原（2026-08-14）

## 结论

`Player_appendPreparedRenderItems_guess` 在非 preview 模式遇到 type-4 particle node 时，
不会先检查该 node 的 accumulated active，也不会为 particle container 自身生成普通
prepared item。四个当前参考二进制都先把 node 的 particle Array Variant 复制、转成一个
独立 AddRef 的 dispatch，并让这一只 owner 同时覆盖 `count` 读取、按 `0..count-1` 的全部
数字索引查询以及对应 child Player 的递归构建；循环结束后才 Release。

这个 owner scope 是可观察语义，不只是优化。Array 的 `count` getter 可以重入清空或替换
node 上的 persistent Variant，但本轮剩余 indexed getter 仍以进入 type-4 分支时保留的同一
dispatch 作为 receiver 和 objthis。本地旧实现的 `node.getParticleCount()` 与每次
`node.getParticleChild(i)` 分别重新借用 Array，可能在重入后切换 receiver；本轮已按四端
恢复成一个分支级 retained owner。

child native pointer 也是 trusted 边界：element native-instance 查询成功仍可能给出 null
native pointer，四端 caller 随即作成员调用，没有 null guard。本地递归 helper 中旧有的
quiet return 已删除；测试不主动触发这个原生崩溃边界。

## 四目标映射

| 目标 | recursive builder | type-4 Array owner acquisition | `count` | indexed child | child recursive call | normal Release |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6BF714` | `0x6C05C0` | `0x6C0A34` | `0x6C0A5C` | `0x6C0A8C` | `0x6BF978` |
| Android armv7 | `0x58B178` | `0x58B2C0` | `0x58B2F6` | `0x58B310` | `0x58B332` | `0x58B33E` |
| iOS arm64 | `0x1001148F8` | `0x100114A10` | `0x100114A5C` | `0x100114A8C` | `0x100114ABC` | `0x100114ACC` |
| iOS armv7 | `0x1123D8` | `0x1127A4` | `0x1127E4` | `0x112806` | `0x11282A` | `0x112838` |

四端的 `count` 都落到已恢复的 `VariantObject_getCount_guess`，indexed child 查询都落到
`ParticleArray_getNativePlayerAt_guess`。本纵切面关注的是 render builder 对这些 helper
外层 owner 的覆盖范围；Array helper 本身、particle update pass 的另一只 owner，以及
spawn/delete/step 生命周期已经单独记录在
`motionplayer_particle_child_lifecycle_four_binary_2026-08-12.md`。

## 控制流位置

共同顺序可还原为：

```text
selectedNode = unchecked priority-selected node
selectedNode.drawnThisFrame = false

if !player.preview and selectedNode.type == 4:
    arrayCopy = CopyRef(selectedNode.particleArrayVariant)
    arrayOwner = arrayCopy.AsObject()                 // independently retained
    arrayCopy.Clear()

    countValue = Void
    arrayOwner.PropGet(0, "count", null_hint,
                       &countValue, arrayOwner)
    count = signed_int(countValue.AsInteger())        // HRESULT ignored

    for i = 0; i < count; ++i:
        child = ParticleArray_getNativePlayerAt_guess(
                    arrayOwner, i)                    // same owner every time
        child.appendPreparedRenderItems(
            callerMain, callerAux,
            selectedNode.inheritFlags & 0x200
                ? effectiveColor
                : 0xFF808080,
            inheritedDrawFlag19,
            inheritedFlag18 || selectedNode.priorDraw)

    arrayOwner.Release()
    continue

if !selectedNode.accumulated.active:
    continue
... type-3 / ordinary source handling ...
```

因此以下边界是四端共同的：

- branch 只在当前 Player 的 `_preview == false` 时成立；preview 中的 type-4 node 不转换
  particle Array，而是继续普通 source/item 路径；
- priority-selected node 的 `drawnThisFrame` 已在 type-4 判断前清零；
- non-preview type-4 在 accumulated active gate 之前处理，inactive particle container 仍会
  构建 children；
- branch 结束无条件 `continue`，container node 自身跳过 active/source/item 处理；
- `count` 是 signed int，循环条件为 `i < count`；零或负数不做 indexed getter/递归，但
  Array owner 仍按正常路径保留并释放；
- 正数按严格升序 `0..count-1` 访问，重复 element 会重复递归，没有去重；
- main/aux 都是 caller 原 vector 引用，children 直接向共享结果贡献 item，没有 child-local
  prepare/sort/wrapper 阶段。

## Array dispatch 的准确生命周期

四端共同 owner 时间线如下：

```text
arrayCopy = CopyRef(node.particleArrayVariant)
arrayDispatch = arrayCopy.AsObject()     // AddRef
arrayCopy.Clear()

count = getCount(arrayDispatch)
for each numeric index:
    child = getNativePlayer(arrayDispatch, index)
    recursively build child

arrayDispatch.Release()
```

`count` 使用 flags 0、null member hint，并令 `objthis == arrayDispatch`；返回 HRESULT 被忽略，
随后使用 result Variant 的 integer conversion。indexed helper 同样使用 flags 0、同 receiver/
objthis 的 `PropGetByNum`，随后把 element Variant 转成对象并查询 Player native instance。

Array Variant 非 object 时，异常发生在 `count` 之前；正常 count/result 和每个 element 的临时
Variant 各自在 helper 内清理。若 count、indexed conversion 或 child recursion 中任一步抛出，
四端 landing pad 都会 Release 当前分支的 retained Array owner；已追加到共享 main/aux 的
child 前缀不回滚。

这里没有 Array 内容快照。retained 的只是 dispatch identity；其 `count` 和 numeric properties
仍可在 getter 或 child recursion 中动态改变。count 值只读取一次，因此后续 Array 增删不会
调整 loop bound，但每个 element 在本轮到达其 index 时从同一 live dispatch 重新读取。

## child Player 参数传播

递归调用的三个显式语义参数四端完全一致：

1. inherited color：node `inheritFlags & 0x200` 非零时传当前 builder 已合成的
   `effectiveColor`，否则传 neutral `0xFF808080`；
2. child draw flag：原样传 caller 的 `inheritedDrawFlag19`；
3. other inherited flag：传 caller 的 `inheritedFlag18` 与当前 particle node 的
   `priorDraw` 布尔值之 OR。

`priorDraw` 属于当前 node，不是 Player 级字段。递归 child 自己仍从其 canonical root Player
读取 draw-affine owner，并在其内部重复 color/priority/type 分支；当前 type-4 owner 一直活到
该 child 调用返回。因此 child 中的脚本 getter 可以清外层 node Array Variant，而不会使外层
下一次 indexed lookup 改用新 dispatch。

## null native pointer 边界

`ParticleArray_getNativePlayerAt_guess` 的 object/native-query 链可能出现三类结果：

- element 不是对象或 class 不匹配：Variant/native conversion 抛异常；
- native query 成功且 adaptor/native 非 null：正常递归；
- query API 成功但 native pointer 为 null：helper 返回 null，builder 立即以其作为 `this`
  进入 recursive builder，没有 quiet skip。

四端 call site 在 child call 前都没有 test/branch。删除本地通用递归 lambda 的
`if (!child) return` 同时影响 type-3 和 type-4 使用者；它恢复的是该 helper 所对应的原生
trusted-pointer 调用边界，而不是为 type-4 特设一个异常。

## 本地修正与测试

`cpp/plugins/motionplayer/PlayerRenderItems.cpp` 已进行以下调整：

1. non-preview type-4 分支构造一次 `ScopedParticleArrayDispatch_guess`；
2. `particleArrayCount_guess` 和全部 `particleArrayGetNativePlayerAt_guess` 都接收同一 owner
   的 raw dispatch；
3. 保留 type-4-before-active、升序 loop、共享 main/aux、inherited-color/flag 传播与无条件
   `continue`；
4. 删除通用 child recursion helper 的 null early return；
5. 删除本段旧 Android arm64 地址/字段偏移注释，以源级语义代替。

新增测试以自定义 Array dispatch 构造两个 root-only child Player。其 `count` getter 在第一次
读取时重入清空 node 的 persistent particle Variant，并严格验证 flags 0、null hint 和 same
objthis；随后仍观察到同一 retained dispatch 收到 numeric indices `{0, 1}`。外部测试 owner
清除后 dispatch 恰好析构一次，证明 builder 没有泄漏 retain。测试将 particle node 设为
inactive，确认递归发生在 active gate 之前；root-only child 利用 builder 的 `nodes.size()<2`
原生早退，不需要伪造 priority owner。

null native pointer 路径没有在单元测试进程内触发，因为四端证据要求保持 unchecked member
call，其预期结果不是可移植 C++ 异常。

## recovery IDB 改善

四个 recovery IDB 均已在对应位置写入：

- non-preview type-4 branch 位于 selected-node active gate 之前；
- CopyRef → AsObject retain → Variant copy clear 的 Array owner acquisition；
- `count` 与全部 numeric getter 共用同一 receiver/objthis；
- child recursive call 无 null guard、共享 main/aux，并按上述规则传播三个参数；
- 正常和异常路径都释放 Array owner。

精确地址只保留在本文，不进入可编译源码注释。

## 验证

- motionplayer 单元测试翻译单元 Emscripten 语法检查通过，仅有既有 `_tss` literal warning；
- `cmake --build --preset "Web Debug Build"` 完成 motionplayer 重编译、静态库链接和最终
  Web 输出链接；
- scoped `git diff --check` 通过，仅报告仓库既有的 LF/CRLF 转换提示；
- 四个 recovery IDB 在 type-4 owner/count/index/recursion/release 注释写回后均已成功保存。
