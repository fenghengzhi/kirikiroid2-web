# ResourceManager::isExistMotion 定向查找、全表回退与 raw PSB 边界四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::isExistMotion` 的共同源码结构是：无条件按 `/` 拆分查询字符串，并立即把
`pieces[1]`、`pieces[2]` 转为 UTF-8 chara/motion key；若 project Variant 非 Void，则要求它已经是
String，以该字符串原样定向查一次 `_loadedModules`；定向节点不存在或没有目标 motion 时，再从
outer unordered-map 的全局迭代链头扫描所有 module。每个 module 都严格读取 raw root 的固定键
`object`，对动态 chara 做 contains gate；chara存在后再严格读取固定键 `motion`，最后对动态
motionName做 contains gate。首个命中返回 true，全表耗尽返回 false。

本地 `ResourceManager.cpp:544..588` 已匹配这一结构，包括几个容易被“安全化”破坏的边界：查询前缀
`pieces[0]` 完全不检查；没有 vector-size gate；非 Void 的非 String project抛Variant转换异常；定向
失败后会从头扫描，因此可能重新检查刚才的同一 module；固定键缺失是异常而不是 false；project key
不执行 `TVPGetPlacedPath`。本轮无需修改运行时 C++。

## 2. 四端 callback 与异常清理

| 平台 | callback | 完整指令 | 独立cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6A6AD8` | 500 | DWARF landing内联/尾部 |
| Android armv7 | `0x57B780` | 187 | 平台EH表驱动 |
| iOS arm64 | `0x100101AC8` | 178 | 平台LSDA驱动 |
| iOS armv7 | `0xFECF4` | 273 | `0xFEFC2`，134条、19个SjLj状态 |

四个 callback 都完成 fresh decompile和full disassembly，均无截断。iOS armv7 cleanup也完成fresh
decompile/full disassembly；它按call-site状态释放split vector、两个UTF-8 `std::string`、定向key
`ttstr`、direct/fallback路径的临时 `PSBRawNode`，再 `_Unwind_SjLj_Resume`。未知状态19走abort，
对应cleanup自身的不可恢复异常边，而不是业务返回分支。

## 3. split、编码与 raw-node helper 映射

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `splitTtstr_guess` | `0x695114`，141条 | `0x571C50`，78条 | `0x1000F52D0`，92条 | `0xF1D20`，163条 |
| `ttstr::AsStdString`路径 | `0x5B5E7C`，90条 | `0x4EF09C`，69条 | `0x100049494`，47条 | `0xF0708`，94条 |
| `GetDictionaryValueStrict` | `0x599038`，63条 | `0x4DD49C`，61条 | `0x1000EDA48`，42条 | `0xE9D10`，82条 |
| `ContainsDictionaryKey` | `0x5999B8`，65条 | `0x4DD918`，41条 | `0x1000EDEF0`，28条 | `0xEA120`，61条 |

16个helper均完成fresh decompile/full disassembly。split helper以可变remainder工作：每找到一个separator
就push该prefix，把remainder替换为separator后的substring；找不到时仍push最后remainder。因此：

- 空字符串得到一个空元素；
- 尾随 `/` 会保留最后空元素；
- 连续 `//` 会保留中间空元素；
- 空 separator在当前ttstr规则下找不到，结果仍为一个元素；
- vector增长失败时，已经发布的ttstr元素由标准容器EH路径逆序释放。

UTF-8 helper先计算转换长度，再resize并写入；null ttstr holder或转换helper返回 `-1` 会构造空
`std::string`。分配失败继续抛出。Android libstdc++与iOS libc++的string layout/SSO不同，但共同
源码仍是 `value.AsStdString()`。

strict raw lookup先从PSB name table解析key index，再从当前dictionary解析value offset；任一步失败都
构造key并抛 `psb: undefined object key '%1' is referenced.`。contains只对dictionary内部tag执行动态
查找：已知非dictionary tag返回false，未知内部tag走internal-error异常；它不是strict getter的
无异常替代品。

## 4. 共同源码伪代码

```text
bool ResourceManager::isExistMotion(Variant projectKey, ttstr path):
    pieces = split(path, "/")
    chara = toUtf8(pieces[1])
    motionName = toUtf8(pieces[2])

    if projectKey.type != Void:
        projectString = requireExactString(projectKey)  // other types throw
        direct = loadedModules.find(ttstr(projectString))
        if direct != end:
            root = RawNode(direct.value.file)
            objects = root.strict("object")
            if objects.contains(chara):
                motions = objects.strict(chara).strict("motion")
                if motions.contains(motionName):
                    return true

    for entry in loadedModules:
        root = RawNode(entry.value.file)
        objects = root.strict("object")
        if !objects.contains(chara):
            continue
        motions = objects.strict(chara).strict("motion")
        if motions.contains(motionName):
            return true

    return false
```

map find使用与load/unload完全相同的cached `ttstr_hash` 和精确 `ttstr_equal`。四端direct find目标
分别为 Android arm64 `0x6E8CD4`、Android armv7 `0x5A7284`、iOS arm64 `0x100139AA8`、
iOS armv7 `0x139CEC`。源级应保留unordered-map find/range-for，不应手写bucket/global node链。

## 5. 查询字符串的未检查边界

callback从不读取 `pieces[0]`，所以 `motion/chara/name`、`anything/chara/name` 甚至其他任意非空
prefix都使用同一chara/motionName。第四段及以后也完全忽略。

它还在检查project Variant之前就读取 `pieces[1]` 和 `pieces[2]`：

- 少于两个separator的输入会在vector边界之外取元素；四端release构建均无显式size检查；
- project为Void不会保护这种短路径；
- `a//b`产生空chara key，`a/b/`产生空motion key，并按正常PSB dynamic key路径查询；
- UTF-8转换失败把对应dynamic key变为空字符串，然后继续查询；
- path本身不做placed-path normalization，因为它是PSB内逻辑路径，不是storage key。

本地保留直接 `pieces[1]`/`pieces[2]`，没有把边界改成false或异常包装。测试必须用真实fixture观察，
不能以伪造vector越界结果作为稳定语义。

## 6. project Variant 与direct/fallback规则

- 只有exact Void跳过direct lookup。String直接复制其UTF-16内容构造map key。
- Integer、Real、Object、Octet等非Void类型进入“需要String”转换错误；没有 `AsString()`格式化或
  `ToString`宽容转换。现有整数测试确认异常。
- String project key不经过 `TVPGetPlacedPath`，所以它必须与load后规范化并存入map的实际key精确
  相等；错误大小写、未规范化alias和不存在key都视作direct miss。
- direct map hit但chara/motion miss不会直接返回false，而会进入完整map扫描；scan从容器begin开始，
  不从direct节点之后开始，也不排除direct节点。
- 因此同一module在direct raw检查返回普通miss后可能再检查一次。若第一次和第二次之间存在未定义
  并发修改，结果没有同步保证；单线程下重复检查只影响成本和潜在异常时序。

full scan沿unordered-map全局迭代链。Android libstdc++与iOS libc++的bucket policy和node顺序不同，
而返回值只表达“任一命中”；在有效只读map上共同语义是range-for。若多个损坏module中有的会抛、
有的会命中，则非标准迭代顺序可能改变先命中还是先异常，这属于损坏/ABI边界，不能写成稳定排序。

## 7. raw PSB导航与owner生命周期

每个候选module的读取顺序固定：

1. 从缓存 `PSBFile` holder构造root raw node并AddRef owner；
2. strict读取固定 `object`；缺失立即抛，不跳过module；
3. contains检查动态chara；false时释放临时raw owner并继续；
4. chara存在时strict读取动态chara node，再strict读取固定 `motion`；任一步缺失/类型异常都传播；
5. contains检查动态motionName；true立即清理所有temporary并返回true。

函数只读raw nodes，不创建 `PSBValueDispatch`、Array、Dictionary或ObjSource，也不修改cache/spec。
所有 `PSBRawNode` 都是短生命周期 owner-retaining pair；direct和fallback两段各自重新构造，不跨段
复用。map record若异常地持有null `PSBFile`，root构造后的严格导航会走四端共同的null/无效owner
尖锐边，不会被当成普通false。

## 8. 异常、返回与并发边界

- empty map且project Void：split/两次UTF-8转换后返回false。
- direct命中目标：在进入full scan前返回true。
- direct miss但其他module命中：full scan返回true。
- 所有module都没有dynamic chara/motionName：返回false；固定key缺失或已知结构不满足strict getter时
  则抛异常，不返回false。
- split/vector/string分配、Variant类型检查、raw key解析和owner AddRef相关异常均无catch，依平台
  DWARF/LSDA/SjLj路径清理后继续传播。
- `_loadedModules`无锁；与load/unload/unloadAll并发迭代或读取owner构成unordered-map/引用生命周期
  data race。函数的const不提供线程安全。
- 没有日志、路径转换、cache更新、`_spec`读取或RandomGenerator副作用。

## 9. 本地逐行对照

`cpp/plugins/motionplayer/ResourceManager.cpp:544..588` 与四端共同结构逐项相同：

- split后立即narrow第1/2动态段；
- `projectKey.Type() != tvtVoid` 才走direct；
- `AsStringNoAddRef`保持exact String类型边界；
- direct `_loadedModules.find(ttstr(projectString...))` 无placed-path normalization；
- direct失败后range-for完整map；
- 两段都按 `strict("object") -> contains(chara) -> strict(chara) -> strict("motion") ->
  contains(motionName)`；
- 只返回bool，不发布raw node或script object。

保留两段看似重复的raw导航，而不抽成会改变temporary生命周期、异常点或遍历起点的缓存lambda；也不
将direct hit/miss折成单次range-for，因为project优先级会影响异常先后。本轮无需C++语义修正。

## 10. 验证状态

`tests/unit-tests/plugins/motionplayer-dll.cpp:27126` 已覆盖：实际project key direct命中、Void full scan、
缺失String project回退命中、缺失motion false、Integer project抛异常；同一fixture还让findMotion
验证回退报告实际map key。它与本轮四端模型一致。

本轮完成四端主callback、split/UTF-8/strict/contains helper和iOS armv7 19-state cleanup的fresh
decompile/full disassembly；四端IDB已命名、注释、添加书签并原位保存；NCB输出确定性重生成、严格
TSV字段检查和 `git diff --check` 已通过。

当前环境缺少CMake/Ninja/Emscripten和完整依赖头，不能宣称正式unit/Web build。工具链恢复后应补跑
现有真实fixture，并增加：任意prefix/额外segment、空dynamic key、direct module普通miss后重复扫描、
固定key损坏抛异常等测试；短路径越界只能作为sanitizer边界观察，不能伪造确定结果。
