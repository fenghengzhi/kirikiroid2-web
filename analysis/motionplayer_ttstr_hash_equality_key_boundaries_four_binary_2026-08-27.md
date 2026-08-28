# motionplayer `ttstr` hash / equality / key boundary 总审计（四参考二进制，2026-08-27）

## 1. 任务结论

本 slice 逐项闭合 `MP-C14`：

- motionplayer 所有 `ttstr` unordered key 使用同一套 32-bit UTF-16 code-unit hash；
- 容器 wrapper 先读共享 backing 的 mutable `Hint`，非零值原样信任，只有零值才计算并发布；
- null-backed `ttstr` 的容器 hash 是 `0`；nonnull allocated-empty 的 hash 是
  `UINT32_MAX`；
- equality 依次判断 backing identity、one-null、Length、UTF-16 payload；
- ordered key 使用 null-first、unsigned UTF-16 code-unit lexicographic comparator；
- unique map/set 的 duplicate 命中保留第一次插入的 key owner；`operator[]` 只返回/覆盖
  mapped value；multimap 则有意保留每个 duplicate node；
- Android old-libstdc++ 与 iOS libc++ 的 bucket/header/node/link/rehash 展开不同，但上述
  key 语义来自同一个共同源码模型。

本地 `internal/ttstr_hash.h` 的生产语义已经与四端一致，不需要修改生产 C++。本轮新增一项
集中单元测试，直接锁住 raw/ttstr 两层 hash、Hint alias、null/allocated-empty、case-sensitive
hash vector、backing-aware equality 和 unordered duplicate 首键保留。

## 2. fresh 四端证据范围

本轮对 30 个 distinct function ranges 全部 fresh decompile、完整 disassembly 和
`xrefs_to`。合计 1,512 条完整且未截断的指令，所有 disassembly cursor 均
`done=true`；fresh xref denominator 合计 2,252。这里的大 xref 数主要来自共享 TJS hash、
equality 和 UTF-16 compare helper，不是 motionplayer 自己存在数千个容器。

### 2.1 unordered 代表路径

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| timeline state `operator[]` | `0x685060` / 75 | `0x5669AC` / 60 | `0x1001A6938` / 148 | `0x1A6074` / 237 |
| payload/container hash helper | 上项内联 | `0x497AFA` / 28 | `0x100039AEC` / 21 | `0x3798C` / 28 |
| backing-aware equality | find 内联 | `0x497BA0` / 41 | `0x10002E518` / 39 | `0x675B8` / 40 |
| state bucket find | `0x534364` / 60 | `0x497B46` / 34 | 上项内联 | 上项内联 |
| loaded-module find | `0x6E8CD4` / 60 | `0x5A72CE` / 34 | `0x100139AA8` / 68 | `0x139CEC` / 72 |

timeline state `operator[]` 同时闭合 hash/Hint、duplicate hit、miss node allocation、key
CopyRef 和 platform rehash；loaded-module find 则交叉证明不同 mapped type、不同 root container
仍使用完全相同的 cached-hash-first / equality-second key path。

### 2.2 ordered `NodeLabelMap` 路径

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `NodeLabelMap::operator[]` | `0x6B2498` / 77 | `0x581C54` / 44 | `0x100141740` / 40 | `0x142844` / 42 |
| lower-bound/find slot | 前项内联 | `0x5ACFA4` / 26 | `0x1001417E0` / 41 | `0x1428B2` / 42 |
| `ttstr` less | 前项内联 | `0x4A98C4` / 24 | `0x100049AB4` / 24 | `0x480F4` / 24 |
| core UTF-16 compare | `0x9B07D0` / 18 | `0x72A318` / 23 | `0x10036446C` / 19 | `0x3671EC` / 23 |

四个 operator helper 都只有 `buildNodeTree` 的一个 caller。Android arm64 把 lower-bound 和
null comparator 展开在 operator helper 内；另外三端保留不同层级的 STL/helper calls。这是内联
差异，不是 comparator 差异。

## 3. 共同 hash 伪代码

四端共同源码语义可写成：

```text
hash_payload_utf16(p):
    acc = 0u32
    for each nonzero uint16 code_unit c:
        mixed = acc + c                    // u32 wrap
        x = 1025 * mixed                   // u32 wrap
        acc = x ^ (x >> 6)
    acc = 9 * acc
    h = 32769 * (acc ^ (acc >> 11))
    return h != 0 ? h : UINT32_MAX

hash_ttstr(s):
    if s.backing == null:
        return 0
    if s.backing.Hint != 0:
        return zero_extend(s.backing.Hint)
    h = hash_payload_utf16(s.payload)
    s.backing.Hint = h
    return zero_extend(h)
```

关键边界：

1. 运算全部是 32-bit unsigned wrap。64-bit target 最后只是把 32-bit hash zero-extend 为
   `size_t`，不是用 64-bit 重新混合。
2. hash 输入是原始 UTF-16 code units，到第一个 `0x0000` 停止；不做 UTF-8 转码、Unicode
   normalization、surrogate 合并或 case folding。
3. empty payload 使最终 `h==0`，随后强制变成 `0xFFFFFFFF`，以便 `Hint==0` 始终保留
   “尚未计算”的含义。
4. null-backed key 在 wrapper 层直接返回 `0`，不进入 payload helper。因此它与
   allocated-empty 的 bucket hash 明确不同。
5. `Hint` 属于 refcounted backing，不属于 `ttstr` handle。CopyRef alias 会观察同一 Hint
   address 和同一次 publication。

本地 `ttstr_hash_utf16` 与 `ttstr_hash::operator()(ttstr)` 正好保持这两个层次。raw pointer
overload 对 null pointer/empty payload 都返回 sentinel；容器面对 null-backed `ttstr` 时由
上层 wrapper 截获并返回零。

## 4. equality 的 backing 边界

四端 collision path 的 equality 完全一致：

```text
equal(a, b):
    if a.backing == b.backing:
        return true
    if a.backing == null or b.backing == null:
        return false
    if a.backing.Length != b.backing.Length:
        return false
    return utf16_strcmp(a.payload, b.payload) == 0
```

由此得到以下不是“普通字符串直觉”的边界：

| 左 key | 右 key | equality | hash |
|---|---|---:|---:|
| null-backed empty | null-backed empty | true（同为 null identity） | `0` / `0` |
| null-backed empty | allocated-empty | false（one-null） | `0` / `UINT32_MAX` |
| 两个独立 allocated-empty | allocated-empty | true（Length=0、payload equal） | `UINT32_MAX` / `UINT32_MAX` |
| 两个独立相同非空 payload | 相同 payload | true | 相同 canonical hash |
| `A` | `a` | false | 不同；无 case folding |

普通 `ttstr()`、null pointer 和普通 `TJS_W("")` 构造会形成 null backing；
`tTJSStringBufferLength(0)` 则故意分配一个 Length=0 的 nonnull backing。参考容器没有把后者
重新 normalise 成 null。

## 5. ordered comparator 与 allocated-empty

`NodeLabelMap` 和 `ParameterRampMap` 共用的 ordered comparator 是：

```text
less(a, b):
    if a.backing == null:
        return b.backing != null
    if b.backing == null:
        return false
    return unsigned_utf16_strcmp(a.payload, b.payload) < 0
```

所以 null-backed empty 严格排在 allocated-empty 之前，两者是两个不同 ordered keys；两个
独立 allocated-empty 则互不 less，构成同一 equivalence class。非空比较逐个减无符号 16-bit
code unit，例如 high surrogate `0xD800` 排在 BMP private-use `0xE000` 之前；比较器不会先
形成 Unicode scalar value。

## 6. duplicate-key 行为分类

### 6.1 unique unordered map/set

常规 canonical Hint 条件下，先比较 cached node hash，再调用 backing-aware equality。命中时：

- 不分配新的 persistent node；
- 不替换 node 内第一次插入的 key backing；
- map `operator[]` 返回现有 mapped slot，caller 可覆盖 mapped value；
- set insertion 返回旧 element，不新增 duplicate；
- later equal input 若 Hint 原为零，会在 lookup 前计算并发布自己的 Hint；即使没有插入，
  later input 仍能观察到这一 side effect。

timeline metadata 的 duplicate label 因而复用已有 state node，只由 caller 后写
`rawElement`；decoded state/owner 不会因为 key duplicate 自动 reset。controller resolver 的
duplicate key 后写 mapped locator，但原 deque element owner 继续存活。ResourceManager 的
duplicate insert 路径会销毁未采用的 candidate/value owner，persistent key 仍是首个 key。

### 6.2 ordered unique map

`NodeLabelMap::operator[]` 只有 miss 才 new node、CopyRef key、value-initialize mapped int；
duplicate 返回旧 node。`buildNodeTree` 随后把 mapped index 改成 later node index，因此查询
解析到最后一次 index，但 tree node 内仍持有第一次 key backing。

### 6.3 ordered multimap

`ParameterRampMap` 是 `std::multimap`，不是 unique map。producer 无条件插入每个 parameter
entry，duplicate id 保留多个独立 tree nodes；binder 的 `equal_range` 逐个消费所有重复项。
这里“首键保留/后写覆盖”不适用。

## 7. poisoned Hint 前置条件

四端都无条件信任任意非零 Hint，不验证它是否等于 payload 的 canonical hash。若外部/损坏状态
让两个 equality-equal 的独立 backings 带有不同非零 Hint，hash/equality contract 已被破坏：
不同 bucket 下可能出现 payload 相等的两个 unique nodes。参考实现不会修复、清零或重新计算。

本地保持同一行为。测试只锁住“非零 Hint 被原样返回”，不把 contract 已破坏后的具体 node
数量写成 portable expectation，因为最终结果还取决于 libstdc++/libc++ bucket count 和 layout。

## 8. publication、allocation failure 与生命周期

unordered insertion 的可见顺序是：

```text
read/possibly publish input backing Hint
    -> bucket selection
    -> cached-hash/equality lookup
    -> miss: allocate candidate/node
    -> CopyRef key backing
    -> construct mapped value
    -> optional rehash
    -> link node and increment size
```

因此：

- hash publication 早于 node allocation；之后 allocation 抛出仍会留下 input backing Hint；
- duplicate hit 不新增 persistent key owner；
- miss node 持有 key CopyRef，直到 erase/clear/dtor Release；
- mapped construction或 rehash 抛出时，标准库 cleanup 释放尚未 link 的 key/value/node；已经
  发布的 Hint 不回滚；
- successful rehash 只移动 bucket/link topology，不替换 key backing 或清 Hint。

ordered miss 同样只在 insertion 成功后发布 tree node；duplicate 没有 key owner transfer。
Android 某些 libstdc++ emplace specialization 会先造 candidate、再发现 duplicate并销毁；iOS
libc++ 常先定位再分配。这改变临时 AddRef/Release 和 allocation frontier，不改变成功后的
首键保留语义。该 STL-wide 差异继续由 `MP-C16` 总审计归档。

## 9. 本地容器 inventory

以下所有 unordered `ttstr` containers 都显式使用同一 `ttstr_hash/ttstr_equal`：

- Engine：mirror hit/miss sets、instant-variable set、controller ref map、variable range map、
  timeline state map、variable value map及短生命周期 label set；
- Player：HM1 EvalCascadeMap、HM2 LabelValueMap、HM3 PerNodeLayerStateMap、HM4
  VariableSnapshotMap；
- ResourceManager：loaded module map，以及每个 record 的 Win/KRKR source texture maps。

ordered containers 则明确使用 `ttstr_utf16_less`：unique `NodeLabelMap` 与 duplicate-retaining
`ParameterRampMap`。源码没有任何 motionplayer `ttstr` container 落回默认 `std::hash<ttstr>`、
`std::less<ttstr>`、UTF-8 `std::string` 或 case-insensitive comparator。

## 10. 本地逐行对照与新增回归测试

生产对照：

- `cpp/plugins/motionplayer/internal/ttstr_hash.h:27`：exact payload mix 与 zero sentinel；
- `cpp/plugins/motionplayer/internal/ttstr_hash.h:48`：shared Hint wrapper、null hash、publication；
- `cpp/plugins/motionplayer/internal/ttstr_hash.h:72`：backing-aware equality；
- `cpp/plugins/motionplayer/internal/ttstr_hash.h:91`：null-first unsigned UTF-16 comparator；
- `cpp/plugins/motionplayer/internal/player_containers.h:29`：Player 四个 unordered maps；
- `cpp/plugins/motionplayer/internal/player_containers.h:68`：ordered map/multimap；
- `cpp/plugins/motionplayer/EmoteEngine.h:115`：Engine set/map aliases；
- `cpp/plugins/motionplayer/ResourceManager.h:63`：ResourceManager nested/outer maps；
- `cpp/core/tjs2/tjsString.h:167`：backing identity/one-null/Length/payload equality；
- `cpp/core/tjs2/tjsString.h:382`：shared Hint address；
- `cpp/core/tjs2/tjsString.h:390`：`IsEmpty` 是 backing-null test；
- `cpp/core/tjs2/tjsVariantString.cpp:546`：ordinary empty allocation normalizes to null；
- `cpp/core/tjs2/tjsVariantString.cpp:594`：buffer-length path保留 allocated-empty backing。

测试对照：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:19876`：ordered null/allocated-empty、duplicate
  首键 backing、unsigned UTF-16 order；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:19919`：新增 unordered raw/ttstr hash 分层、Hint
  alias/trust、exact `A/a` hash vectors、equality、null/allocated-empty 两 key 和 duplicate
  首键 backing。

新增测试不改变生产 ABI 或容器语义。`git diff --check` 已通过；正式 unit/Web build 和 runtime
执行仍由独立 `MP-V06/MP-V07` 验证任务统一完成。

## 11. IDB 固化

四个 IDB 本轮完成 18 项 helper rename、19 条 task comments 和 4 个 task bookmarks。命名包括：

- `ttstr_unordered_hash_guess`；
- `ttstr_backing_aware_equal_guess`；
- `ttstr_unordered_state_find_guess`；
- `ttstr_unordered_loaded_module_find_guess`；
- `ttstr_utf16_less_guess`；
- `TJS_utf16_compare_guess`。

四库均在证据、命名、comments 和 bookmarks 固化后保存。

## 12. `MP-C14` disposition

| 要求 | 结论 |
|---|---|
| exact hash | 四端 exact 32-bit UTF-16 mix、zero sentinel、64-bit zero-extension已闭合 |
| equality | backing identity / one-null / Length / payload 顺序已闭合 |
| null key | unordered hash 0；ordered 排第一；与 allocated-empty 不相等 |
| allocated-empty | unordered hash `UINT32_MAX`；nonnull empty 之间相等；ordered 与 null 分离 |
| duplicate unique key | mapped 后写、首 key owner 保留；set 不新增 node |
| duplicate multimap key | 每项独立 node、`equal_range` 全部保留 |
| Hint | alias-shared、zero means uncomputed、nonzero trusted、failure不回滚已发布 Hint |
| case/UTF-16 | code-unit、case-sensitive、无 normalization；更广 truncation 专项留给 `MP-B05` |
| platform STL | 成功后共同语义一致；candidate/link/rehash 差异归 `MP-C16` |
| local implementation | 生产代码已一致；新增集中单元测试；无 task-local 静态缺口 |

因此 `MP-C14` 可标记为 `CLOSED_STATIC`。正式构建、测试执行与浏览器 runtime 不是本 ticket
重复占用的静态缺口，继续由 `MP-V` 类任务跟踪。
