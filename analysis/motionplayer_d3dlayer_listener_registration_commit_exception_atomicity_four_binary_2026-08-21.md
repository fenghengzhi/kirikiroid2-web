# MotionPlayer D3DLayer listener 注册提交与异常原子性四参考闭环（V267）

## 1. 结论

本轮闭合 V266 留下的一个精确问题：`D3DLayerListener` 构造期间调用
`owner->AddListener(this)` 时，list node 分配失败会不会留下半链接或 size 不一致的
listener registration。

四个参考给出完全一致的 source-level 结论：

1. null listener 直接返回，不分配、不写 list；
2. non-null listener 先调用 throwing `operator new(0x18/0x0C)`；
3. 分配成功后先完整初始化 detached node 的 link/payload；
4. 最后才修改 sentinel link；iOS 再以一个紧随其后的 store 发布 cached size；
5. 分配之后的成功路径只有 load/store/branch，没有第二个 call、用户 callback 或
   source-language throw point；
6. 因此 allocation failure 时 list links、cached size 与节点集合都保持调用前状态，
   不存在半注册 listener；
7. listener base constructor 此时虽然没有完成、不会调用自己的 destructor，但也没有
   registration 需要 `RemoveListener` 回滚；
8. D3DEmote shell constructor 在 AddListener 成功后也没有后续可抛调用，所以
   “constructor 抛出并遗留已注册 listener”在四参考中不可达；V266 的真实 listener
   泄漏边界仍然是 constructor 已正常返回以后，inner `EmoteObject::clone` 抛出。

本轮同时把 `RemoveListener` 的内部容器差异闭合：Android 使用 libstdc++ 无 cached-size
list，iOS 使用 libc++ 带 cached size 的 list；Android arm64 内联逐节点删除，Android
armv7 调用带 value-alias 保护的 specialization，而 iOS 把连续匹配区间 splice 到临时
list 后统一释放。

## 2. 四端函数映射

| 目标 | AddListener | RemoveListener | remove specialization | hook/splice | unhook/temporary clear |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x531184` | `0x5311C8` | 内联于 Remove | hook `0x145EFDC` | unhook `0x145EFF8` |
| Android armv7 | `0x495286` | `0x4952AC` | `0x497C26` | hook `0xD3B884` | unhook `0xD3B894` |
| iOS arm64 | `0x1002336C8` | `0x100233720` | `0x100236510` | splice `0x1002365F4` | clear `0x10023374C` |
| iOS armv7 | `0x232572` | `0x23259A` | `0x23520C` | splice `0x235308` | clear `0x2325B0` |

iOS armv7 的 remove SJLJ cleanup 为 `0x2352DE`。

本轮用于把构造链与 AddListener 对接的 registration call site：

| 目标 | shell/listener-base 构造位置 | 虚 AddListener call |
|---|---:|---:|
| Android arm64 | clone 内联构造 `0x53039C` | `0x5303F0` |
| Android armv7 | `D3DEmotePlayer_ctor_guess` `0x497824` | `0x49784A` |
| iOS arm64 | `D3DEmotePlayer_ctor_guess` `0x100236300` | `0x100236340` |
| iOS armv7 | `D3DEmotePlayer_ctor_guess` `0x235022` | `0x235052` |

四端 call 都取 owner vtable slot 6：LP64 byte offset `0x30`，ILP32 byte offset
`0x18`。它位于 base vptr/borrowed owner/stretch type/bicubic bias 写入之后、derived
slots/scalars/flags 与最终 derived vptr 写入之前。

## 3. list ABI

### 3.1 Android：libstdc++，无 cached size

| 项 | LP64 | ILP32 |
|---|---:|---:|
| list sentinel 相对 D3DLayerObject | `+0x28` | `+0x18` |
| node allocation | `0x18` | `0x0C` |
| node.next | `+0x00` | `+0x00` |
| node.prev | `+0x08` | `+0x04` |
| node.payload | `+0x10` | `+0x08` |
| cached size | 不存在 | 不存在 |

hook-before primitive 的共同语义为：

```cpp
node->prev = position->prev;
node->next = position;
position->prev->next = node;
position->prev = node;
```

Android arm64 的 helper 只有 7 条指令，armv7 只有 6 条；均无 call。unhook 只有
5 条指令：

```cpp
node->prev->next = node->next;
node->next->prev = node->prev;
```

### 3.2 iOS：libc++，带 cached size

libc++ node 的双向 link 字段顺序与 Android 相反：

| 项 | LP64 | ILP32 |
|---|---:|---:|
| list sentinel 相对 D3DLayerObject | `+0x28` | `+0x18` |
| sentinel.prev | list `+0x00` | list `+0x00` |
| sentinel.next | list `+0x08` | list `+0x04` |
| cached size | object `+0x38` | object `+0x20` |
| node allocation | `0x18` | `0x0C` |
| node.prev | `+0x00` | `+0x00` |
| node.next | `+0x08` | `+0x04` |
| node.payload | `+0x10` | `+0x08` |

iOS AddListener 把 hook 与 size increment 全部内联，没有独立 list helper call。

## 4. AddListener 精确提交顺序

共同 source shape 仍然是：

```cpp
void D3DLayerObject::AddListener(D3DLayerListener *listener) {
    if(listener)
        Listeners.push_back(listener);
}
```

### 4.1 Android arm64

```text
0x531194  listener==null -> return
0x531198  sentinel = self+0x28
0x5311A0  operator new(0x18)       // 唯一 throw point
0x5311A4  node.next=node.prev=null
0x5311A8  node.payload=listener
0x5311B8  tailcall hook(node,sentinel)
```

allocator 抛出时，sentinel 尚未被读取用于写回，更未被修改。hook 是 no-call leaf，返回
即表示完整提交；Android ABI 没有 size store。

### 4.2 Android armv7

```text
0x49528E  listener==null -> return
0x495292  operator new(0x0C)       // 唯一 throw point
0x495298  node.next=node.prev=null
0x4952A0  node.payload=listener
0x4952A6  tailcall hook(node,self+0x18)
```

顺序与 arm64 完全相同；仅 pointer width/node size/sentinel offset 不同。

### 4.3 iOS arm64

```text
0x1002336E0  listener==null -> return
0x1002336EC  operator new(0x18)       // 唯一 throw point
0x1002336F0  node.next=sentinel; node.payload=listener
0x1002336F4  oldTail=sentinel.prev
0x1002336F8  node.prev=oldTail
0x1002336FC  oldTail.next=node         // link commit begins
0x100233700  sentinel.prev=node
0x100233704  oldSize=list.size
0x100233708  newSize=oldSize+1
0x10023370C  list.size=newSize         // successful commit ends
```

`oldTail.next` 与 `sentinel.prev` 之间、link 与 size 之间都没有 call。单线程 C++
observable model 下没有重入观察窗口；异步 signal/data race/memory corruption 不属于该
API 的定义边界。

### 4.4 iOS armv7

```text
0x23257A  listener==null -> return
0x23257E  operator new(0x0C)       // 唯一 throw point
0x232582  node.payload=listener
0x232588  node.next=sentinel
0x23258A  oldTail=sentinel.prev
0x23258C  node.prev=oldTail
0x23258E  oldTail.next=node
0x232590  sentinel.prev=node
0x232592  oldSize=list.size
0x232594  newSize=oldSize+1
0x232596  list.size=newSize
```

与 iOS arm64 是逐字段相同的 ILP32 lowering。

## 5. 失败状态矩阵

| 阶段 | node storage | sentinel links | iOS size | constructor 状态 | 可观察结果 |
|---|---|---|---|---|---|
| listener 为 null | 无 | 不变 | 不变 | 继续 | no-op |
| `operator new` 抛出 | 分配未返回 | 不变 | 不变 | base ctor 未完成 | exception；无 registration |
| detached node 初始化 | live pending node | 不变 | 不变 | AddListener 内 | 无 source throw point |
| link stores | live/published | 顺序写入 | 尚未递增或 Android 无该字段 | AddListener 内 | 无 call/throw/reentry |
| iOS size store | published | 完整 | `old+1` | AddListener 即将返回 | 成功 registration |
| AddListener 返回 | list owner 持有 node | 完整 | 一致 | constructor 继续 | 后续只有 scalar/vptr stores |

需要特别区分两层 C++ 语义：

- base constructor 在自身抛出时不会自动调用自己的 destructor；
- 但这里唯一 throw 点位于外部副作用之前，所以没有 destructor rollback 也不会遗留 node；
- outer `new D3DEmotePlayer` landing 只 scalar-delete pending shell storage，与该边界一致；
- AddListener 成功以后 constructor 剩余指令无可抛调用，因此 constructor-failure landing
  不可能在“已注册”状态触发；
- inner clone 是 constructor 返回后的另一调用，失败时 raw shell 已完成且没有 owner cleanup，
  所以 V266 的 shell+listener 泄漏仍然成立。

`operator new` 可能调用全局 new-handler，但在 allocator 返回前当前 list 仍未修改；即使
handler 重入观察 owner，也只会看到调用前状态。

## 6. RemoveListener 内部形态

共同公开语义为：

```cpp
void D3DLayerObject::RemoveListener(D3DLayerListener *listener) {
    if(listener)
        Listeners.remove(listener);
}
```

null 是 no-op，non-null 删除所有相等 payload，而不是只删除首项。

### 6.1 Android arm64：内联 live-list erase

`0x5311C8` 从 sentinel.next 开始扫描。每轮先在 `0x5311F4` 保存 next，再比较 payload；
匹配时调用 unhook、scalar delete，然后使用预取 next 继续。因此 duplicate node 会全部删除，
删除后不读取已释放 node。

### 6.2 Android armv7：alias-aware specialization

wrapper `0x4952AC` 把 listener 保存为 stack value，再调用 `0x497C26`。specialization 同样
预取 next，并保留标准容器算法的 value-alias guard：

```cpp
if (&node->payload == valueRef)
    defer_this_node;
```

D3D wrapper 的 `valueRef` 指向栈，因此该 guard 在本调用链不可达，但它证明 helper 是
libstdc++ 通用 `list::remove(const T&)` 形状，而不是插件手写的单项删除循环。

### 6.3 iOS：splice 到临时 list 后统一释放

iOS 两端 remove specialization 先在栈上构造：

```cpp
temporary.prev = &temporary;
temporary.next = &temporary;
temporary.size = 0;
```

扫描时识别连续相等 payload 区间 `[first,last)`，调用 range-splice：

1. 计算区间 node count；
2. `source.size -= count`；
3. `temporary.size += count`；
4. 从 source 摘下整个区间；
5. 把区间挂到 temporary 尾部；
6. 扫描结束后 clear temporary，先 detach/zero size，再逐 node scalar delete。

整个 splice helper 无 allocation、无 callback、无 call。payload 是 borrowed listener pointer，
clear 只释放 list node，不删除 listener 对象。

### 6.4 iOS armv7 SJLJ

armv7 `0x23520C` 在 splice 前把 `call_site` 写为 1。dispatcher/cleanup `0x2352DE`
的有效 case0 会：

```text
clear temporary list
call_site = -1
_Unwind_SjLj_Resume(exception)
```

default/impossible state终止。这个 landing 表达旧 libc++/编译器对 out-of-line splice call
的 unwind owner，但 concrete `0x235308` 只有 load/store/branch，实际没有 source-language
throw edge。它不改变 AddListener 的“allocator 是唯一 throw point”结论。

## 7. 源码对比与改动

portable 源码的两个 executable statement 已经正好是参考 source shape：

```cpp
Listeners.push_back(listener);
Listeners.remove(listener);
```

因此本轮没有手写 node allocator、sentinel stores、cached size 或 rollback catch，也没有把
list 改成 vector/snapshot。手写这些平台 lowering 反而会固定错误 ABI，并改变标准库异常语义。

本轮只在 `cpp/plugins/DrawDeviceD3D.cpp` 补入 ABI-neutral 注释：

- base construction 中 registration allocation failure 不发布半节点；
- push_back allocation-before-link/size 的强边界；
- Android live erase 与 iOS splice-to-temporary remove 的 ABI 分叉。

同时原位收紧两份旧分析：

- `motionplayer_d3dlayer_object_listener_container_lifecycle_four_binary_2026-08-15.md`；
- `motionplayer_nested_clone_state_variant_raw_copy_listener_shell_unwind_four_binary_2026-08-21.md`。

后者现在明确区分“shell constructor failure 无 listener residue”和“constructor 返回后 inner
clone failure 泄漏 completed shell+listener”。

## 8. Recovery IDB 写回

四库合计写回：

- 48 条 function/line comment；
- 8 个 bookmark；
- 20 个 `_guess` function rename；
- 19 个 function type/prototype update；
- 24 个定向 force-recompile/readback。

| 目标 | comments | bookmarks | renames | type updates | 关键恢复 |
|---|---:|---:|---:|---:|---|
| Android arm64 | 9 | 2 | 4 | 4 | Add/Remove + hook/unhook |
| Android armv7 | 11 | 2 | 5 | 5 | alias-aware remove specialization |
| iOS arm64 | 13 | 2 | 5 | 5 | Add link/size + remove splice/clear |
| iOS armv7 | 15 | 2 | 6 | 5 | remove SJLJ cleanup |

最终 recovery IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 366802431 | `D8B5C17D5B92CBE787408BADB44A561798A0D404D9C91152D715F8B8901EE3ED` |
| Android armv7 | 345998908 | `9393B879ACF81C5D2220AE4D0DBD20F4724CE5E52A82F396EE78D4A4C258F171` |
| iOS arm64 | 334966683 | `BDCE74E127F4852FA99AA64B3FDC518429B2094D84FB1704078FD58A2C4F9500` |
| iOS armv7 | 376893273 | `DB5790FBD217931EF85AEF41B92D7F60FC7E714A74BC5A39710445C52081EF09` |

iOS armv7 安全保存：

| 状态 | bytes | SHA-256 |
|---|---:|---|
| canonical/pre-backup/candidate initial | 376835929 | `6264553BF0715CE6C510CAEFBB7AE4674C88D04B6FF8010EDB86EA0EE4447CA9` |
| final candidate/canonical | 376893273 | `DB5790FBD217931EF85AEF41B92D7F60FC7E714A74BC5A39710445C52081EF09` |

candidate 编辑前后均经 `C:\IDA\idat.exe -A`，两次 exit 0。发布后 candidate/canonical
size 与 SHA-256 一致；再从 canonical 独立重开，回读 Add/Remove、remove/splice/clear、
SJLJ cleanup 六个名字及 Add/remove/cleanup fresh decompile，最后 `save=false` 关闭。

## 9. 验证状态

本轮源码改动只有注释，没有 executable behavior 改动。验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 test-TU syntax-only 均 exit 0；
- Web Debug 增量构建 3/3、Wasmtime Debug 增量构建 4/4、Wasmtime guest 1/1 均成功；
- 随后 Web、Wasmtime、guest 三目标顺序复跑均为 `ninja: no work to do`；
- `git diff --check` exit 0；仅输出工作树已有的 LF→CRLF conversion warning；
- compiled source 的本轮 reference absolute-address 定向扫描无命中；
- `mcp__idalib__idb_list` 返回 `sessions=[]`, `count=0`；进程审计也没有残留
  `ida`/`ida64`/`idat`/`idat64`/`idalib-worker` GUI 或 worker 进程。

最终 Wasm：

| 产物 | bytes | SHA-256 |
|---|---:|---|
| Web `libkrkr2.wasm` | 85655322 | `6039AA6D8DC48FB7CCC5840CFF7630EEE9838C1AB2809BCEE5B096BCD42EEC6F` |
| Wasmtime `libkrkr2.wasm` | 85002463 | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` |
| Wasmtime guest | 151479098 | `63B4217BC27C06B83B0211FEC469CBE1A8A4D7A6CAE6D05F6568A7232FA785F8` |

关键 section size：

| 产物 | CODE | DATA | name |
|---|---:|---:|---:|
| Web | `0x01A4109D` | `0x005A3E40` | `0x03185F7B` |
| Wasmtime | `0x019E904B` | `0x005A1090` | `0x03141E11` |
| Wasmtime guest | `0x013D7DCD` | `0x004D1630` | `0x01421EBA` |

Web 与 Wasmtime 主产物相对 V266 逐字节不变。guest 的总 bytes 与上述 executable/data/name
section size 也和 V266 相同，但完整 SHA-256 改变；本轮只移动注释对应的 source line，guest
仍携带大体积 DWARF，因此这是 debug line mapping 变化，不能误报为 executable section 内容变化，
也不能仅凭相同 section size 断言 CODE 内容逐字节相同。

## 10. 闭合范围

V267 已闭合：

- listener-base constructor 到虚 AddListener 的 precise throw frontier；
- Android/iOS、LP64/ILP32 四种 node/sentinel/size lowering；
- allocation-before-publication 与不存在 half-registration 的证明；
- D3D shell constructor failure 不遗留 listener 的 V266 收紧结论；
- RemoveListener 的 Android live erase、armv7 alias guard、iOS temporary splice/clear；
- iOS armv7 remove SJLJ temporary owner。

这不代表 motionplayer 全目标已经完成；下一纵切面继续沿高价值 raw owner、容器 ABI、异常
回滚、回调重入与对象生存期边界推进。
