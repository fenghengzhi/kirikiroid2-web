# MotionPlayer Engine HM1/HM2 mirror cache 四参考二进制复原（2026-08-14）

## 范围与本轮修正

本纵切面重新以 `reference/binaries/` 的 Android arm64、Android armv7、
iOS arm64、iOS armv7 为准，闭合：

- mirror pattern `vector<ttstr>`、HM1 positive set、HM2 negative set 的相邻布局；
- 构造、metadata clear、normal dtor 与 ctor unwind 的所有权顺序；
- `buildMirrorControl` 的 property hint、vector append 和部分提交；
- `shouldMirrorLabel` 的 gate/cache/pattern scan/insert 数据流；
- `IndexOf` 第一个 occurrence、空 pattern、cache stale、gate toggle 和异常边界；
- HM1/HM2 const-key insert 与 HM4 rvalue insert 在 Android old-libstdc++ 上的真实差异。

本轮发现并修正两个旧结论/遗漏：

1. 四端的 `variableMatchList` PropGet 都传入同一个逻辑上的独立进程级 hint；本地原来
   传 null。
2. `IndexOf(pattern, 0) >= 1` 比较的是**第一个** occurrence。若 pattern 在首字符出现，
   即使后面再次出现，返回值仍为 0，仍然是 miss。旧文档“第二字符或更后出现就匹配”
   过宽，现已收紧。

HM1/HM2 与 HM4 的表头、节点、hash/equality 基础结构详见
`analysis/motionplayer_emote_hm4_instant_set_four_binary_2026-08-14.md`；本文只重复与
mirror cache 数据流直接相关的部分。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Engine ctor | `0x67B76C` | `0x560948` | `0x1001B7FB0` | `0x1B7788` |
| clear patterns + HM1/HM2 | `0x666A98` | `0x5559C8` | `0x1001A6664` | `0x1A5D54` |
| metadata reset caller | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |
| mirror builder | `0x66C744` | `0x558C24` | `0x1001AB4F4` | `0x1AABCC` |
| mirror predicate | `0x679A90` | `0x55F8FC` | `0x1001B37C4` | `0x1B3394` |
| const-key set insert | `0x689320` | `0x569C94` | predicate 内联 | predicate 内联 |
| script `setMirror` | `0x66F190` | `0x55A336` | `0x1001AD644` | `0x1ACCEA` |
| Engine dtor | `0x67C898` | `0x5610E8` | `0x1001B8B4C` | `0x1B814E` |

四份 IDB 中 clear helper 现统一命名为
`EmoteEngine_clearMirrorPatternsAndCaches_guess`；Android 的 const-key insertion 统一为
`EmoteTtstrSet_insertUniqueConstKey_guess`。名称带 `_guess`，因为 strip 后没有原始源码
符号可直接证明拼写。

## 相邻成员布局

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| pattern vector | `+800` | `+400` | `+480` | `+240` |
| HM1 positive set | `+824` | `+412` | `+504` | `+252` |
| HM2 negative set | `+880` | `+440` | `+544` | `+272` |
| HM3 timeline map | `+936` | `+468` | `+584` | `+292` |

pattern vector 是自然 `vector<ttstr>`：64 位 24 字节、32 位 12 字节，均为
begin/end/capacity 三指针。HM1/HM2 是两个彼此独立、同 specialization 的
`unordered_set<ttstr>`：

| 参考 | set header | node | 初始 buckets |
|---|---:|---:|---|
| Android arm64 | 56 B | 24 B `{next,key,hash}` | 构造请求 10，选 prime 11 |
| Android armv7 | 28 B | 12 B `{next,key,hash}` | 构造请求 10，选 prime 11 |
| iOS arm64 | 40 B | 24 B `{next,hash,key}` | 0，首次 miss 惰性扩桶 |
| iOS armv7 | 20 B | 12 B `{next,hash,key}` | 0，首次 miss 惰性扩桶 |

两个 set 不共享 bucket allocation、first-node、size 或 max-load 状态。它们只共享
源级 hash/equality 类型和可复用的库 helper。

## 构造、clear 与析构顺序

### 构造

成员构造顺序为：

```text
pattern vector -> HM1 positive -> HM2 negative -> HM3
```

Android 两端为 HM1、HM2 各自立即分配 11 桶；HM2 bucket allocation 失败时，ctor
unwind 会销毁已经完成的 HM1 和 vector。iOS 两端只把两个表头清零、写
`max_load_factor=1`，所以这两步本身没有 bucket allocation。

### metadata clear

四端专用 helper 的精确顺序为：

```text
release pattern[0..size) and set vector.end = vector.begin
clear HM1 positive
clear HM2 negative
```

vector capacity 和 allocation 保留。两个 set clear 都释放每个 owned key 和节点，
清零 bucket predecessor entries/first-node/size，但保留 bucket allocation/count 与
rehash policy；它们不是用新空容器替换旧容器。

这个 helper 只由 metadata reset 调用。正常 `applyMetadata` 先执行 reset，随后才读取
新的 metadata mirror/base byte 和 `mirrorControl`，所以正式替换路径不会把旧 cache
带入新 pattern list。

### normal dtor

声明逆序为：

```text
HM3 -> HM2 negative -> HM1 positive -> pattern vector
```

因此 final destruction 与 metadata clear 的顺序不同。HM2/HM1 full dtor 在释放
key/node 后再释放 bucket storage；最后 vector 释放剩余 pattern key 和 buffer。每个 set
node 和每个 vector element 都各自拥有一个 ttstr backing 引用。

## mirror builder 与专用 TJS hint

四端共同伪代码：

```cpp
list = mirrorControl.PropGet(
    0, "variableMatchList", mirrorVariableMatchListHint);
count = list.count;
for (int i = 0; i < count; ++i) {
    raw = list[i];
    patterns.push_back(ttstr(raw));
}
```

hint 地址：

| 参考 | `mirrorVariableMatchListHint_guess` |
|---|---:|
| Android arm64 | `0x1AB4F78` |
| Android armv7 | `0x1111510` |
| iOS arm64 | `0x101B6A028` |
| iOS armv7 | `0x187DA48` |

四端 xref 都落在 mirror builder 的该次 PropGet；它不与顶层 metadata `mirror` property
的 hint 共用。slot 是进程级可变 `tjs_uint32`，不是 Engine 成员或栈变量。

builder 没有 enabled gate、类型过滤、去重或 local clear。它保留：

- 输入顺序；
- 重复 pattern；
- null/empty ttstr；
- 入口时已有的 vector 内容；
- 入口时已有的 HM1/HM2 cache。

inline-capacity push 先把 backing pointer 写进 end、CopyRef，再推进 end；growth helper
分配新 buffer、复制/retain 既有和新元素、成功后发布并释放旧 buffer。局部 temporary
随后 Release。读取、转换或 growth 失败没有 builder 级 rollback：此前已追加项保留，
尚未成功的当前项不应被源码补偿性跳过。

## `shouldMirrorLabel` 完整数据流

四端共同源级算法：

```cpp
if (!mirrorChanged)
    return false;

if (HM1_positive.find(label) != end)
    return true;
if (HM2_negative.find(label) != end)
    return false;

for (const ttstr &pattern : patterns) {
    if (label.IndexOf(pattern, 0) >= 1) {
        HM1_positive.insert(label);
        return true;
    }
}

HM2_negative.insert(label);
return false;
```

derived gate 与三个 mirror byte 的四端偏移为：

| byte | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| requested | `+1156` | `+588` | `+788` | `+404` |
| metadata/base | `+1157` | `+589` | `+789` | `+405` |
| changed/XOR gate | `+1158` | `+590` | `+790` | `+406` |

gate 是第一条业务分支。false 时不会读取 vector、不会查询 set、不会计算/写 label
backing Hint，也不会分配。它只返回 false。

gate true 后 positive 优先于 negative。正常单线程路径只会把一个 label 插入其中一个
set，所以两者保持不相交；若损坏/外部调试使同一 key 同时存在，positive 的优先查询
决定返回 true。

## `IndexOf` 的精确边界

原始调用四端都等价于 `label.IndexOf(pattern, 0)`。ttstr 实现首先要求两边 backing
非 null，再以 UTF-16、大小写敏感的 `strstr` 类搜索返回第一个 occurrence：

- 无 occurrence：`-1`，miss；
- first occurrence `0`：miss；
- first occurrence `>=1`：match；
- pattern 先在 0 出现、后面再次出现：仍返回 0，miss；
- null-backed label 或 pattern：`-1`，miss；
- 非 null empty pattern：搜索返回起点 0，miss。

例如 pattern=`"pre"`：

| label | first IndexOf | cache |
|---|---:|---|
| `"prefix"` | 0 | HM2 negative |
| `"prepre"` | 0 | HM2 negative |
| `"xprefix"` | 1 | HM1 positive |

vector 按原顺序扫描。重复 pattern 在此前都 miss 时会重复执行同一次搜索；没有 pattern
hash、prefix tree 或去重辅助结构。

## cache insertion 与 HM4 的关键差异

HM1/HM2 的插入参数是既有 `const ttstr &label`。Android old-libstdc++ 为此生成的 helper
顺序是：

```text
compute/reuse label Hint/hash
find existing key
  hit : return existing node; no allocation, no CopyRef
  miss: allocate 24/12-byte node -> CopyRef label -> rehash/link -> size++
```

iOS libc++ 的内联路径也是 find-before-allocation。`shouldMirrorLabel` 在选择结果前已经
查过目标 cache 一次，随后 `insert(label)` 按容器 API 再查询一次目标 set；因此 uncached
结果正常会经历一次 predicate lookup 和一次 insert-internal lookup。

这与 HM4 Android builder 的 `insert(ttstr(value))` rvalue 路径不同：HM4 会先分配/retain
候选节点，再查重复。于是：

- HM1/HM2 duplicate insert 四端都不需要节点 allocation；
- HM4 duplicate insert 只有 iOS 不分配，Android 仍可能在候选 allocation 处抛异常。

不能仅因三者源类型同为 `unordered_set<ttstr>`，就把不同 value category 生成的插入
边界合并成一个手写流程。

## cache stale、gate toggle 与直接调用

- `buildMirrorControl` 直接追加 pattern，不清 HM1/HM2。若一个 label 已在 negative
  cache，后来直接追加可匹配 pattern，negative lookup 仍先返回 false；反向的 positive
  cache 同样覆盖后续 pattern 删除/替换。
- `setMirror` 只写 requested/changed、更新 Player root flip 并 reset controllers；它不清
  vector 或 cache。
- gate 从 true 变 false 时，所有 query 暂时返回 false，但旧 positive/negative node
  继续存在。
- gate 再变 true 时立即复用旧 cache，不重新扫描 pattern。
- 只有 metadata reset 或 Engine destruction 清理这些 cache。正常 metadata replacement
  因为 reset 在 builder 前执行，所以不会发生 direct-call stale 边界。
- 没有单 key erase、cache expiry、capacity shrink 或脚本可见 cache enumeration。

## 异常与并发边界

- gate-off、positive hit、negative hit 都不做节点 allocation；cache miss 的最终 insert
  可能在 node/bucket allocation 或 rehash 处抛异常。
- positive cache insert 失败时不会回退去写 negative；negative insert 失败时不会吞掉
  异常再返回 false。
- builder 或 predicate 都不事务化；先前成功 append/cache 项保留。
- key 被 set node CopyRef 后独立于调用方 label temporary 生存，直到 clear/dtor Release。
- ttstr 引用计数/Hints 有目标自己的原子细节，但 unordered set/vector 本身没有 Engine
  级锁。并发修改/查询同一 Engine 的这些容器不属于线程安全接口；端口不应额外加锁并
  改变原始时序，除非另立兼容层契约。

## 源码、测试与 IDB 落地

- `EmoteEngine.cpp` 新增 `mirrorVariableMatchListHint_guess` 并传给 mirror builder 的
  PropGet；收紧 first-occurrence 注释并明确 cache 跨 gate toggle 保留。
- `EmoteEngine.h` 补齐 HM1/HM2 四端偏移、clear/dtor 边界和 first-occurrence 语义。
- `motionplayer-dll.cpp` 增加 `"prepre"` prefix-repeat miss 与 gate false->true 后复用
  positive cache 的定向断言。
- 旧 clamp/mirror 总览文档同步修正 first-occurrence 描述并补 hint 四端映射。
- 四份 recovery IDB 新增/统一 clear helper、Android const-key insert 和 hint 全局名/类型，
  并在 ctor/builder/predicate/setMirror/dtor 上保存本纵切面注释。

## 验证

源码与四份 recovery IDB 落地后完成以下验证：

- 完整 `motionplayer-dll.cpp` 测试翻译单元通过 Emscripten `-fsyntax-only` 检查；仅有仓库
  已存在的 `_tss` literal-operator 空格弃用 warning。
- `cmake --build --preset "Web Debug Build"` 完成 8 个步骤，重新编译 motionplayer 相关
  对象并成功产出最终 `index.html`/Wasm；输出只有既有 `_tss`、`imagepacker.h`
  `nodiscard` 与 Emscripten 链接 warning。
- 对本纵切涉及的源码、测试、分析文档与 `plan.md` 执行 `git diff --check`，退出码为 0；
  只报告工作区既有的 LF/CRLF 转换提示，没有 whitespace error。
- 定向 `rg` 检查确认源码/测试/相关 mirror 文档中没有回流旧 `sub_6696B8`、
  `sub_68BF40`、`sub_689760`、笼统“第二次出现”语义或把 mirror-list PropGet 继续传
  `nullptr` 的旧写法；只命中新加入的 `mirrorVariableMatchListHint_guess` 定义、调用和
  本文说明。

## 2026-08-16：Mirror builder nested accessor 补证

对四个 `buildMirrorControl` 重新反编译后，builder 的 sole property read 已从 portable raw
helper 展开恢复为源码级双 accessor：copied input 构造 root `ncbPropAccessor`，
`variableMatchList` direct-temporary 再构造 nested accessor。Count 只快照一次，每项直接用 indexed
`GetValue<ttstr>` 后 append；尾部严格 nested list accessor 先于 root accessor release。新增可重入
owner-drop probe 锁定 getter HRESULT 忽略、exact objthis、Count snapshot 和 list-before-root 析构。
详细地址、UTF-16LE literal 和四端 cleanup 表见
`analysis/motionplayer_mirror_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。
