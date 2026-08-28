# ResourceManager::findSource `src`/`blank` 分流、ObjSource发布与字典owner四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::findSource` 先按 `/` 拆分path，再按首段做两个精确分支：`src`分支把第2、
第3段转成UTF-8 group/icon key，只在传入的moduleKey对应缓存记录中严格导航
`root["source"][group]["icon"][icon]`，命中后新建一个持有raw icon node、texture初始为null的ObjSource，
通过非sticky、非throwing的NCB adaptor发布；`blank`分支把第2段再按 `:` 拆成四个未经数值转换的
String Variant，创建Dictionary并依次写 `width/height/originX/originY`，最后写 Integer `blank=1`。
其他首段、普通map/group/icon miss都返回Void。

本地 `ResourceManager.cpp:406..508` 已匹配两个分支、未检查segment/dimension访问、exact module lookup、
strict/contains边界、Dictionary字段类型/顺序/flags/hints，以及ObjSource adaptor失败泄漏规则。本轮无需
修改运行时C++。

## 2. 四端主 callback与cleanup

| 平台 | callback | 完整指令 | 独立cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6A7F1C` | 646 | DWARF landing内联/尾部 |
| Android armv7 | `0x57BDE0` | 262 | 平台EH表驱动 |
| iOS arm64 | `0x100102594` | 227 | 平台LSDA驱动 |
| iOS armv7 | `0xFF890` | 374 | `0xFFC8C`，175条、29个SjLj状态 |

四个callback均fresh decompile并完成full disassembly，无截断。iOS armv7 cleanup也完整读取；它覆盖
主split vector、`src`的两个UTF-8 string与raw-node链、`blank`的dimension vector、Dictionary/accessor
dispatch、ObjSource allocation/constructor和adaptor publication。dictionary/accessor destructor若在
cleanup中再次抛出，状态27/28进入terminate；状态29是cleanup不可恢复的abort边界。

## 3. UTF-16原始字节核验

Hex-Rays在iOS两端和Android arm64把宽字面量错误显示为单字母 `s`/`o`。本轮按ASCII/UTF-8、
UTF-16LE、UTF-32LE搜索流程直接用UTF-16LE bytes搜索，所有pattern游标均完成；主callback引用的精确
宽字面量为：

| 字面量 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `src` | `0x14D50A2` | `0xD84CB8` | `0x10195B2DA` | `0x174D63E` |
| `blank` | `0x14D5284` | `0xD84E2C` | `0x10195B540` | `0x174D8A4` |
| `width` | `0x14C72EE` | `0xD7B808` | `0x10195B5D8` | `0x174D93C` |
| `height` | `0x14E12FA` | `0xD8E3C4` | `0x10195B5E4` | `0x174D948` |
| `originX` | `0x14D5318` | `0xD84EC0` | `0x10195B5F2` | `0x174D956` |
| `originY` | `0x14D5328` | `0xD84ED0` | `0x10195B602` | `0x174D966` |

每个地址均由raw byte match和callback data xref共同确认。`source`、`icon`是PSB raw ASCII key；`/`、
`:`和Dictionary属性名是TJS宽字符串。不能把反编译器的 `s`/`o` 当成真实脚本名或平台特化。

## 4. 共同源码伪代码

```text
Variant ResourceManager::findSource(ttstr moduleKey, ttstr path):
    pieces = split(path, "/")

    if pieces[0] == "src":
        groupKey = toUtf8(pieces[1])
        iconKey = toUtf8(pieces[2])
        record = loadedModules.find(moduleKey)
        if record == end:
            return Void

        root = RawNode(record.file)
        source = root.strict("source")
        if !source.contains(groupKey):
            return Void
        iconHolder = source.strict(groupKey).strict("icon")
        if !iconHolder.contains(iconKey):
            return Void
        iconEntry = iconHolder.strict(iconKey)

        src = new ObjSource(iconEntry)  // raw owner retained, texture=null
        dispatch = ObjSourceAdaptor.CreateAdaptor(src, sticky=false, throw=false)
        if dispatch == null:
            return Void                // src deliberately not reclaimed
        result = VariantObjectClosure(dispatch, dispatch)
        dispatch.Release()
        return result

    if pieces[0] == "blank":
        dims = split(pieces[1], ":")
        dictionary = new Dictionary accessor
        dictionary.SetValue("width",   String(dims[0]), MEMBERENSURE, widthHint)
        dictionary.SetValue("height",  String(dims[1]), MEMBERENSURE, heightHint)
        dictionary.SetValue("originX", String(dims[2]), MEMBERENSURE, originXHint)
        dictionary.SetValue("originY", String(dims[3]), MEMBERENSURE, originYHint)
        dictionary.SetValue("blank",   Integer(1),      MEMBERENSURE, blankHint)
        return VariantObjectClosure(dictionary.dispatch, dictionary.dispatch)

    return Void
```

`pieces[0].IsEmpty()` 的本地早返回与二进制先比较 `src`/`blank` 后返回Void等价；两者都不会在未知
首段访问pieces[1]。源码级分支不应改成正则、basename或path normalization。

## 5. `src`分支的数据流和边界

### 5.1 输入与map

- 只有首段精确等于小写 `src` 才进入；大小写、空首段、`source`等均返回Void。
- 进入后先读取并narrow `pieces[1]`、`pieces[2]`，再查module map；`src`或`src/group`没有size gate，
  即使moduleKey不存在也先触发未检查vector索引/转换边界。
- 第4段及以后忽略；连续separator可产生空group/icon，并按空UTF-8 key正常查询。
- `moduleKey`本身是typed ttstr参数，直接用于 `_loadedModules.find`；不执行 `TVPGetPlacedPath`，也不
  像findMotion那样full-scan fallback。
- map find复用四端 `0x6E8CD4` / `0x5A7284` / `0x100139AA8` / `0x139CEC` 精确hash/equality路径。

### 5.2 raw PSB导航

固定 `source` strict读取，缺失或结构错误直接抛异常。dynamic group先contains；false返回Void。group
存在后strict读取group，紧接着strict读取固定 `icon`；这里没有对固定icon做contains gate，所以缺失
会抛。dynamic icon再contains；false返回Void；true后strict取得最终iconEntry。

group temporary在构造iconHolder完整表达式结束时先释放；source、iconHolder、iconEntry分别以
`PSBRawNode` owner pair持有raw file allocation。map中null/损坏file holder没有防御性false路径。

## 6. ObjSource结构与NCB adaptor owner

ObjSource在此处的共同源结构只有两个owner字段：复制/AddRef后的raw icon node，以及null lazy texture。
对象大小由ABI产生：64位 `0x18`，32位 `0x0C`；不应写padding复刻。

adaptor链的fresh完整映射为：

| 平台 | CreateAdaptor核心 | Variant wrapper |
|---|---:|---:|
| Android arm64 | `0x6E9504`，92条 | callback内联 |
| Android armv7 | `0x5A7A04`，82条 | `0x57C178`，30条 |
| iOS arm64 | `0x10013A190`，65条 | `0x100102AB4`，29条 |
| iOS armv7 | `0x13A274`，115条 | `0xFFE54`，67条 |

四组均fresh decompile/full disassembly。调用参数固定为 `sticky=false, throw=false`：

- ObjSource class object不存在或CreateNew失败：核心返回null，wrapper发布Void；新建src没有owner，泄漏其
  allocation与raw PSB引用。
- script object创建成功但NativeInstanceSupport类型不兼容/返回null native：核心仍返回script object，
  不把src写入adaptor；wrapper仍发布该对象，src同样泄漏。
- 类型兼容：native instance保存src指针，sticky保持false；script adaptor失效/析构时负责delete src，
  src destructor释放lazy texture（若后来创建）和raw owner。
- CreateAdaptor内部脚本分配/调用抛出时无src scope guard，异常传播且src泄漏。

每次命中都新建ObjSource/adaptor；不写入 `winSourceTextures`/`krkrSourceEntries`，不复用此前facade。

## 7. `blank`分支与Dictionary字段

`blank`分支完全忽略moduleKey，不需要已load任何PSB。它直接读取 `pieces[1]` 并按 `:` split，随后
未检查访问dims[0..3]；少于4项属于vector越界尖锐边，额外项忽略。负号、小数、空串和任意文本都不
解析，四个维度保持String Variant。只有marker是Integer 1。

Dictionary创建和setter helper均fresh完整读取：

| 平台 | Dictionary create | String SetValue | Integer SetValue |
|---|---:|---:|---:|
| Android arm64 | `0x9C6D40`，70条 | callback内联；String Variant ctor `0xA0E72C`，19条 | callback内联；Variant copy `0xA0E464`，67条 |
| Android armv7 | `0x7384A8`，68条 | `0x4E24EC`，44条 | `0x4E2568`，51条 |
| iOS arm64 | `0x1000A7A38`，50条 | `0x100102B3C`，32条 | `0x100102BD0`，38条 |
| iOS armv7 | `0xA6900`，99条 | `0xFFF28`，62条 | `0xFFFF8`，68条 |

Dictionary factory使用guarded进程单例class object，零参数CreateNew新Dictionary。五次PropSet均以
`TJS_MEMBERENSURE == 512`、同一dictionary dispatch作objthis，并各用独立全局member-hint slot。
每个值先构造fresh临时Variant，PropSet后立即析构；普通非零status只让setter wrapper返回false，
callback忽略该bool并继续后续字段。抛出的异常则按已创建字段的partial-commit状态传播，不回滚字典。

返回时同一Dictionary dispatch组成Object/ObjThis closure并释放factory/accessor本地引用。结果每次
调用都是新Dictionary；没有共享blank缓存。

## 8. 普通miss、异常和副作用

- unknown/empty prefix：Void，不访问moduleKey或pieces[1]。
- `src` map miss、group contains miss、icon contains miss：Void；已创建string/raw temporaries正常释放。
- `src`固定key strict失败、UTF转换/分配失败、ObjSource/adaptor脚本异常：无catch传播。
- `blank`普通PropSet失败可返回部分Dictionary；异常PropSet不返回，已写字段仍留在即将由owner清理的
  dictionary对象中。
- 函数不改module map、SourceCache、spec、RandomGenerator、layer-id或texture cache；没有日志。
- module map无锁；`src`与load/unload并发是container/raw owner data race。`blank`只共享Dictionary class
  singleton和hint words，guard初始化线程安全性由各平台runtime决定，hint更新遵循TJS现有规则。

## 9. 本地逐行对照与测试

`cpp/plugins/motionplayer/ResourceManager.cpp:406..508` 已保留：

- `src`优先、`blank`次之、unknown Void；
- `src`先narrow两个dynamic key再exact module find；
- `source/group/icon/iconEntry`的strict/contains顺序；
- raw icon复制进texture-null ObjSource；CreateAdaptor null不delete src；
- `blank`四个String字段、Integer marker、flags 512和五个独立hint；
- 每次fresh Object closure与正确Release。

`tests/unit-tests/plugins/motionplayer-dll.cpp:27090` 使用真实fixture验证：两个blank调用产生不同Dictionary；
四个维度Type均为String且保留 `640/480/-3/4`；blank为Integer 1；unknown/empty prefix、module miss和
group miss返回Void；正常src路径由同一fixture后续资源读取覆盖。无需C++修正。

本轮还完成UTF-16LE六组pattern四端全游标搜索与callback xref核验、四端IDB命名/注释/书签/原位保存、
NCB确定性重生成、严格TSV字段和 `git diff --check`。当前缺少CMake/Ninja/Emscripten和完整依赖头，
不能宣称正式unit/Web build；工具链恢复后应运行现有fixture并补adaptor class缺失/类型不兼容与
Dictionary PropSet partial-failure注入测试。
