# Motion.D3DAdaptor 正常析构与 software-texture map 节点生命周期（四参考，2026-08-15）

## 1. 结论

四个当前参考二进制共同恢复出下面的 source-level destructor：

```text
~D3DAdaptor():
    softwareTextureCopies.clear()
    if targetTexture != null:
        targetTexture.Release()
        targetTexture = null
    if window != null:
        window.Release()
    softwareTextureCopies.~map()   // normal path sees an empty tree
```

关键边界是：

- map key 只是 borrowed `iTVPTexture2D *` identity，不 AddRef、不 Release、节点销毁时也不
  dereference；
- mapped value 是 `TJS::tTJSRefHolder<iTVPTexture2D>`，每个节点先对 mapped texture 调用
  `Release`，成功返回后才 delete node；
- explicit clear 完成所有节点后才把 tree header 发布为空；
- target 也是先 Release、返回后才把 target 槽清零；
- Window Release 前后 raw Window 槽都保持原指针，native destructor 不写 null；
- destructor body 中 explicit clear 后，C++ 自动 map destructor 再执行一次 tree erase；正常路径
  root 已为 null，因此第二次只是空树 no-op；
- GNU libstdc++ 两端和 libc++ 两端的节点销毁次序不同，这会改变多个 texture 的 Release
  side-effect 顺序。

本地 destructor 原先在 Window Release 返回后额外 `_window = nullptr`；四端均无这次 store，
现已删除。此前 A64 tree helper 被误命名为
`MotionRenderBatch_destroyCallableTree_guess`；完整 xref 和节点 payload 复核证明它只销毁
D3DAdaptor texture-map，已改成 `D3DAdaptor_destroyTextureTree_guess`。

## 2. 根函数与正常顺序地址

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `removeAllTextures` | `0x6AAC98` | `0x57CF74` | `0x100103D58` | `0x101138` |
| destructor | `0x6AAFCC` | `0x57D12E` | `0x1001040A0` | `0x1013BC` |
| first tree erase | `0x6AAFE8` | `0x57D13A` | `0x1001040C4` | `0x1013E8` |
| target Release | `0x6AB008` | `0x57D146` | `0x1001040E0` | `0x101432` |
| target-null store | `0x6AB00C` | `0x57D14A` | `0x1001040E4` | `0x101438` |
| Window Release | `0x6AB020` | `0x57D154` | `0x1001040F8` | `0x101446` |
| automatic map destructor | `0x6AB02C` | `0x57D158` | `0x100104104` | `0x10144E` |

四端在 Window Release 之后都直接进入 map-subobject destructor/函数尾，没有 Window-null
store。目标纹理则明确在 Release 返回后清零。这两个 raw owner 的 publication 时点并不相同。

## 3. 节点布局与 mapped holder

节点销毁 helper 只读取 child link、mapped pointer，并 delete node：

| 目标 | tree helper | mapped pointer offset | mapped Release | node delete |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D8C38` | node `+0x28` | `0x6D8C70` | `0x6D8C78` |
| Android armv7 | `0x59A8EC` / node helper `0x59A918` | node `+0x14` | `0x59A924` | `0x59A92C` |
| iOS arm64 | `0x1001285E4` | node `+0x28` | `0x100128620` | `0x100128630` |
| iOS armv7 | `0x127928` | node `+0x14` | `0x127990` | `0x127994` |

64 位 mapped pointer 在 node `+0x28`，32 位在 `+0x14`；这与一个 pointer-sized key 紧邻
一个 pointer-sized holder 的节点 value payload 一致。helper 从不读取 key payload。因此：

- 插入 source key 不会把 source 的引用所有权转给 map；
- source 本体可以在 map entry 仍存在时被其他 owner 释放，之后 key 只剩 identity bit pattern；
- clear 不会对 key 调任何虚函数；
- mapped texture 每个现存 node 恰好 Release 一次。

工厂 creation reference 在 software miss 插入后没有由 caller Release；mapped holder 只拥有
额外 AddRef 的那一份。因此 clear 节点只能释放 holder reference，不能修复此前遗留的 factory
creation reference。

## 4. GNU 与 libc++ 的 Release 顺序

Android 两端的 GNU tree erase 使用同一算法：

```text
while node != null:
    erase(node.right)       // recursion
    next = node.left
    node.mapped.Release()
    delete node
    node = next
```

地址为 A64 `0x6D8C5C` 和 A32 `0x59A8FE`。对于按 borrowed pointer key 排序的 map，这会按
descending key 顺序触发 mapped texture Release。

iOS 两端的 libc++ helper 使用：

```text
erase(node.left)
erase(node.right)
node.mapped.Release()
delete node
```

地址为 I64 `0x100128604/0x100128610`、I32 `0x127978/0x127980`。这是 left-right-root
postorder，不保证按 key 单调。

因此同一组 source/copy entries 在 Android 与 iOS 上可能以不同顺序执行 texture Release。
如果 Release 有日志、回调或共享引用计数 side effect，这个差异可观察；可移植源码应保留各
目标标准库自然生成的顺序，不额外把 keys 拷贝出来排序。

## 5. clear 的 commit 点与重入窗口

| 目标 | tree root erase | empty-header publication |
|---|---:|---:|
| Android arm64 | `0x6AACB0` | `0x6AACB8..0x6AACBC` |
| Android armv7 | `0x59A8D6` | `0x59A8DE..0x59A8E2` |
| iOS arm64 | `0x100103D70` | `0x100103D74..0x100103D78` |
| iOS armv7 | `0x101144` | `0x10114A..0x101150` |

header 只有在 recursive erase 全部返回之后才归零/恢复 sentinel。单个节点也是 mapped Release
返回后才 delete。因此 Release callback 发生时：

- 当前节点仍分配着；
- tree header 仍指向原树；
- 已按 traversal 顺序处理的节点已经删除，尚未处理的节点仍在树链接中；
- 重入 `removeAllTextures`、插入或查询同一 map 不受 guard 保护，属于原生不安全重入边界。

正常返回后 header 一次性变成 empty；`removeAllTextures` 不触碰 target、Window、尺寸、中心或
五个 script-visible Boolean。

## 6. target 与 Window Release 的可观察状态

destructor 先把 map 清空，再处理 target 和 Window：

1. target Release callback 期间，map 已为空，但 `targetTexture` 成员仍指向正在 Release 的
   对象；Window 槽仍有效；
2. target Release 返回后才写 target=null；
3. Window Release callback 期间，map 为空、target 已为 null、Window 成员仍指向 callback
   receiver；
4. Window Release 返回后也不清 Window 成员，随后只销毁已经为空的 map subobject。

这解释了为什么本地 `_window = nullptr` 虽处在 destructor 尾部，仍不是一比一结构：它改变了
destructor 返回前最后一个可见 member write，而且掩盖 raw slot 从不回滚/清零的所有权模型。

## 7. destructor 异常边界

target/Window Release 异常的 destructor cleanup 地址为：

| 目标 | map-subobject cleanup | terminate path |
|---|---:|---:|
| Android arm64 | `0x6AB04C` | `0x6AB054` → catch/terminate helper |
| Android armv7 | `0x57D164` | `0x57D16A` → catch/terminate helper |
| iOS arm64 | `0x100104124` | `0x10010412C` → catch/terminate helper |
| iOS armv7 | `0x101484` | `0x10148A` → catch/terminate thunk |

body exception 退出前仍调用 map-subobject destructor，然后进入 terminate；正常情况下 map 已在
body 开头清空，所以这次清理是空树 no-op。

若 target Release 抛出，target-null store 尚未执行，Window Release 也未开始；若 Window
Release 抛出，target 已经 Release 并清零。Window 槽在两种路径中都不清零。

mapped holder destructor 本身没有 recovery commit：A64 和 I32 的 tree helper 分别可见
catch/terminate landing（`0x6D8C94`、`0x1279B4`），其他构建也没有“跳过坏节点继续 clear”的
路径。Release 未返回时 node delete 和 empty-header publication都不会发生。

## 8. 恢复库与源码落地

- 删除 `D3DAdaptor::~D3DAdaptor` 中参考不存在的 `_window = nullptr`；
- 保留 `removeAllTextures -> releaseTargetTexture -> Window Release -> automatic map dtor`
  顺序；
- A64 错误的 callable-tree helper 名改为 texture-tree 语义名；
- 四份 IDB 统一补齐 texture-tree/tree-node/map-subobject 与 destructor cleanup 的保守
  `_guess` 名称；
- 四份 IDB 注释 GNU/libc++ traversal、mapped Release-before-delete、header commit、target/
  Window publication 和 terminate cleanup，并添加书签。

## 9. 验证

- `Web Debug Build`：成功重编 `D3DAdaptor.cpp`、静态库并链接最终 Wasm/HTML；
- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过，仅有仓库既有 `_tss`
  deprecated warning；
- `git diff --check`：通过，仅报告工作树既有 LF/CRLF conversion warning；
- 本纵切面新增/更新文档无 trailing whitespace；
- 四份 recovery IDB：已保存。
