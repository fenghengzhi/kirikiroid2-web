# MotionPlayer 字符串 null、allocated-empty、大小写、UTF-16 NUL 截断与 hash 四参考横向审计

日期：2026-08-28  
原始任务：`MP-B05`

## 1. 结论

四个参考二进制共同证明，motionplayer 的“空字符串”和“字符串 key”不能压缩成
`std::u16string` 的单一值语义。必须保留以下可观察状态与阶段：

1. default/null-backed `ttstr`：backing pointer 为 null，容器 hash 为 0；
2. allocated-empty：backing 非 null、stored Length 为 0、payload 首 unit 为0，容器 hash为
   `UINT32_MAX`；它与 null-backed 不相等、在 ordered map 中是不同 key；
3. 非空 backing：key hash、ordered compare和collision equality都消费原始UTF-16 code units，
   不做UTF-8转换、Unicode normalization、surrogate合并或默认case folding；
4. embedded UTF-16 NUL：payload hash和最终compare在第一个`0x0000`停止；equality会先比较
   stored Length，因此“长度相同、NUL前缀相同、NUL后tail不同”的两个backing可比较相等；对
   pre-sized lowercase目标，NUL还会提前停止copy并留下未写suffix，该uninitialized tail归`MP-B12`；
5. module key是唯一明确的ASCII-only大小写折叠域：只把UTF-16 `A..Z`变成`a..z`，完整路径、
   扩展名和非ASCII units保留；
6. autoload `.tpm`是另一套byte-string `strcasecmp`边界，不能与UTF-16 module-key lower混成
   一个通用case-insensitive helper；
7. ResourceManager path/module、timeline label、node label、source/icon和PSB dynamic key各自保留
   精确case、owner、encoding转换和null/allocated-empty gate。

现有production string/hash/container实现与四端一致。本轮只新增一个集中单元测试，补齐embedded
UTF-16 NUL的stored-length、hash、equality、ordered comparator和ASCII-lower边界，没有修改
production语义。

## 2. 本轮 fresh 四端证据

本轮使用原生`mcp__idalib__*`对64个独立范围重新执行decompile、完整disassembly、strings、
constants/callees及`xrefs_to/from`审计。所有decompile成功、所有disassembly均未截断。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | `xrefs_from` | IDB 更新 |
|---|---:|---:|---:|---:|---|
| Android arm64 | 16 | 4,615 | 597 | 16 | 16条任务注释、1个书签 |
| Android armv7 | 16 | 2,054 | 458 | 16 | 16条任务注释、1个书签 |
| iOS arm64 | 16 | 1,927 | 464 | 16 | 16条任务注释、1个书签 |
| iOS armv7 | 16 | 3,267 | 444 | 16 | 16条任务注释、1个书签 |
| 合计 | 64 | 11,863 | 1,963 | 64 | 64条注释、4个书签；四库原位保存 |

高xref数来自共享TJS hash/equality/UTF-16 comparator被整个宿主程序复用，不代表motionplayer
自身存在同等数量的string容器。审计分母选择timeline unordered key、node ordered key、
ResourceManager cache/query/source resolver和module loader三种彼此独立的consumer族，避免仅凭一个
STL实例推断全局规则。

## 3. 四端函数映射

| 语义范围 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| timeline state subscript | `0x685060`，75 | `0x5669AC`，60 | `0x1001A6938`，148 | `0x1A6074`，237 |
| unordered hash/find | `0x534364`，60（内联hash/equal） | `0x497AFA`，28 | `0x100039AEC`，21 | `0x3798C`，28 |
| backing equality/loaded find | `0x6E8CD4`，60（内联） | `0x497BA0`，41 | `0x10002E518`，39 | `0x675B8`，40 |
| NodeLabelMap subscript | `0x6B2498`，77 | `0x581C54`，44 | `0x100141740`，40 | `0x142844`，42 |
| core UTF-16 compare | `0x9B07D0`，18 | `0x72A318`，23 | `0x10036446C`，19 | `0x3671EC`，23 |
| ResourceManager load | `0x6A616C`，501 | `0x57B338`，246 | `0x1001012D8`，225 | `0xFE40C`，364 |
| ResourceManager unload | `0x6A697C`，87 | `0x57B6F8`，47 | `0x100101A28`，35 | `0xFEC04`，69 |
| ResourceManager isExistMotion | `0x6A6AD8`，500 | `0x57B780`，187 | `0x100101AC8`，178 | `0xFECF4`，273 |
| ResourceManager findMotion | `0x6A72B4`，791 | `0x57B9F8`，262 | `0x100101E84`，255 | `0xFF11C`，396 |
| Player findSourceForNode | `0x691CC8`，1,191 | `0x570500`，676 | `0x1000F316C`，586 | `0xEF97C`，952 |
| LoadModule wrapper | `0x548E24`，34 | `0x4A9648`，28 | `0x100287B38`，16 | `0x28A8A4`，46 |
| lowered LoadModule state machine | `0x701DE8`，99 | `0x5BA8E8`，69 | `0x10029FDE4`，55 | `0x2A48FC`，103 |
| AllRegist line indexing | `0x548EAC`，56 | `0x4A96A0`，51 | `0x100287B8C`，52 | `0x28A950`，97 |
| Plugins.link | `0x9086A0`，38 | `0x6C7E58`，29 | `0x1003F2210`，20 | `0x3D9884`，64 |
| Plugins.getList | `0x9087DC`，288 | `0x6C7F20`，110 | `0x1003F22E4`，99 | `0x3D9A0C`，207 |
| plugin autoload / `.tpm` | `0x907618`，740 | `0x6C77BC`，153 | `0x1003F1CD4`，139 | `0x3D923C`，326 |

## 4. 三编码raw搜索与IDA显示截断

按`ida-search-string`流程，本轮对以下四个代表字符串在四库分别完成UTF-8、UTF-16LE和
UTF-32LE raw pattern搜索，每个cursor均`done=true`：

- `motionplayer.dll`；
- `blank`；
- `.tpm`；
- `timeline label not found '`。

结果按数据域严格分离：

| 字符串 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 结论 |
|---|---|---|---|---|---|
| `motionplayer.dll` UTF-16LE | `0x14D4222` | `0xD84BA8` | `0x10195B980`等2处 | `0x174DCE4`等2处 | 四端宽字符串；UTF-8/UTF-32均0 |
| `blank` UTF-16LE | `0x14D5284` | `0xD84E2C` | `0x10195B540` | `0x174D8A4` | 四端精确宽literal |
| `.tpm` UTF-8 | `0x1510E6E` | `0x6C8124` | `0x101500883` | `0x138F595` | autoload使用byte string；UTF-16/32均0 |
| timeline miss UTF-16LE | `0x14D3AAA` | `0xD844DC` | `0x10195FEEE` | `0x1752252` | 四端完整宽日志literal；UTF-8/32均0 |

随后对每端每类命中读取前4字节、完整payload、双零终止符和后续字节，确认不是pattern落在另一
字符串内部。例如四端`motionplayer.dll`均为连续16个UTF-16LE code units后跟`00 00`；timeline
日志完整26个code units后跟终止符。普通IDA string cache若只显示首字母，是宽literal被错误标注
为ASCII而产生的显示截断，不是reference运行时只有一个字符，也不能作为negative证据。

## 5. null-backed 与 allocated-empty

四端unordered wrapper的共同伪代码：

```text
hash_ttstr(s):
    if s.backing == null: return 0
    if s.backing.Hint != 0: return s.backing.Hint
    h = hash_payload_utf16_until_nul(s.payload)
    s.backing.Hint = h
    return h
```

empty payload的pure hash原本算出0，随后被替换为`UINT32_MAX`，因为Hint=0必须保留“尚未计算”含义。
因此：

| 状态 | backing | stored Length | container hash | 与另一个状态相等？ |
|---|---:|---:|---:|---|
| default/null-backed | null | 0 | 0 | 两个null相等 |
| allocated-empty | nonnull | 0 | `UINT32_MAX` | 两个allocated-empty相等 |
| null vs allocated-empty | 不同 | 都是0 | 不同 | false |

`IsEmpty()`是backing-null测试，不是`Length()==0`。这解释了source resolver中allocated-empty src/icon
为何进入non-null route，而ordinary empty为何进入fallback，以及ordered map为何能同时拥有两个
表面长度为0的key。

## 6. embedded UTF-16 NUL截断

raw payload hash和core comparator都是NUL-terminated：遇到第一个`0x0000`立即停止。backing-aware
equality在此之前检查stored Length：

```text
equal(a,b):
    if same backing: true
    if exactly one backing null: false
    if a.Length != b.Length: false
    return utf16_strcmp_until_nul(a.payload,b.payload) == 0
```

所以：

- `[A,0,X] length=3`与`[A,0,Y] length=3`：hash相同、equality为true、ordered comparator互不less；
- `[A,0,X] length=3`与普通`[A] length=1`：hash相同，但stored Length gate使equality为false；
- `[A,0,X]`与`[B,0,X]`：第一个unit已决定hash/ordering不同；
- NUL后的tail不参与hash/compare，但仍属于backing的Length/lifetime边界；
- AsLowerCase/uppercase循环也在NUL停止，但目标buffer已按原stored Length预分配；循环不补写
  当前terminator，也不初始化剩余suffix。因此malformed embedded-NUL module key的后续hash/compare
  可读到allocator遗留tail，不能把它稳定简化为“NUL前缀lowercase key”。此未初始化局部的完整
  生命周期/平台复刻由`MP-B12`归档，本项只固定string processing frontier。

本轮新增测试直接构造Length=3的backing并锁定稳定的hash/equality/ordered行为，避免以后把compare
替换成length-aware `std::u16string_view`或把hash改成遍历stored Length；对lowercase的测试只使用
well-formed字符串，锁定ASCII-only且非ASCII unit不变，不对未初始化tail伪造确定expectation。

## 7. case-sensitive域

以下reference路径都不做case folding或Unicode normalization：

- motionplayer/Engine/Player的unordered timeline、variable、controller、snapshot keys；
- NodeLabelMap、ParameterRampMap的ordered keys；
- ResourceManager load/unload后的规范化placed path cache key；
- `isExistMotion/findMotion`的project String direct key；
- node label、timeline label、source/icon、raw-label selector和PSB dynamic keys；
- hash本身：`A`与`a`的canonical 32-bit hash分别不同。

“placed path normalization”属于storage resolver，不等于lowercase；load和unload必须对resolver返回的
精确ttstr做同一hash/equality。错误case的project key先direct miss，再按函数各自规则full scan或
fallback，不应在map wrapper里偷偷lowercase。

## 8. 两个明确case-insensitive域

### 8.1 NCB module key

`AllRegist`和`LoadModule`复制完整ttstr，再只对UTF-16 ASCII `A..Z`加32。它不提取basename、不删除
目录、不改扩展名、不折叠`.`/`..`、不处理非ASCII大小写。embedded NUL终止lower循环；若stored
Length仍包含tail，pre-sized目标的剩余units未被copy，属于上节所述malformed/uninitialized边界。

`Plugins.link`把argv0完整交给loader；`Plugins.getList`返回已经lowered的完整stored key。故
`MotionPlayer.DLL`与`motionplayer.dll`命中同一key，但`Dir/MotionPlayer.DLL`仍是
`dir/motionplayer.dll`，不会退化成basename。

### 8.2 autoload extension

autoload扫描的是narrow filename，并用byte-string `strcasecmp`检查最后4 bytes是否`.tpm`。
这不是ttstr hash/equality，也不是module loader UTF-16 lower。Android保留原后缀；iOS把记录名的
末4 bytes替换为`.dll`。两种大小写策略的输入encoding、publication和平台差异必须分开。

## 9. UTF-8/UTF-16转换边界

ResourceManager module cache key始终是ttstr/UTF-16。PSB字典dynamic keys则是narrow/UTF-8：

- query path先按UTF-16 `/` split；
- chara/motion/source/icon在特定route才转换为UTF-8；
- conversion失败由各helper返回/异常规则决定，不会回头修改UTF-16 module key；
- `isExistMotion/findMotion`不会把project String narrow后比较；
- `findSourceForNode`以backing-pointer gate区分src/icon，再在Win/KRKR/generic各route按原顺序转换。

因此不能把所有key统一成`std::string`，也不能为了“大小写兼容”对PSB dynamic key或module cache
做共享lowercase。

## 10. hash、Hint与owner

payload hash是32-bit unsigned wrap mix，输入是到NUL为止的原始uint16 units；64-bit平台只把结果
zero-extend为`size_t`。wrapper先读backing内共享Hint：

- CopyRef aliases共享同一Hint地址；
- 任意非零Hint原样信任，不验证payload；
- zero才计算并在bucket lookup前发布；
- 后续allocation/rehash失败不会回滚已发布Hint；
- duplicate hit保留第一次插入的persistent key backing，只覆盖mapped value；
- null backing没有Hint，直接hash为0。

Android旧libstdc++与iOS libc++展开不同bucket/node拓扑，但上述source-level key语义一致；物理
iteration order继续属于STL ABI边界。

## 11. 本地对照

| 参考要求 | 本地实现 |
|---|---|
| pure UTF-16 hash、NUL终止、zero sentinel | `cpp/plugins/motionplayer/internal/ttstr_hash.h` 的 `ttstr_hash_utf16` |
| null hash、Hint trust/publication | `ttstr_hash::operator()(const ttstr&)` |
| backing identity/one-null/Length/NUL compare | `ttstr_equal`委托`tTJSString::operator==` |
| null-first unsigned UTF-16 ordered compare | `ttstr_utf16_less` |
| null/allocated-empty backing creation | `tjsString.h`与`tjsVariantString.cpp`的普通allocator/buffer-length allocator |
| ResourceManager exact path keys | `ResourceManager.cpp` load/unload/isExistMotion/findMotion |
| source/icon backing gates | `PlayerResource.cpp`的findSourceForNode恢复实现 |
| ASCII-only module lowering | `cpp/core/plugin/ncbind.cpp`与`ncbind.hpp` |
| narrow `.tpm` extension matching | `cpp/core/plugin/PluginImpl.cpp` |

没有发现production偏差。新增测试位于
`tests/unit-tests/plugins/motionplayer-dll.cpp`的embedded UTF-16 NUL test case，并与已有
null/allocated-empty/Hint/case-sensitive hash test、ordered node-label test共同形成回归组。

## 12. 最终判定

`MP-B05`没有剩余task-local静态差异。四端string identity、encoding、case、NUL截断、hash/Hint、
container key owner和consumer route已完成一一映射；64条IDA任务注释、4个书签已写入并保存到四库。

正式native unit、Web Debug和runtime/differential执行统一归入`MP-V01..V08`。
