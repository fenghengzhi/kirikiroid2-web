# ResourceManager::findMotion raw motion dispatch、返回 Array 与异常 owner 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::findMotion` 的查询部分与 `isExistMotion` 同构：无条件拆分path并读取
`pieces[1]/pieces[2]`，非Void project必须是String，先定向查module，再完整遍历map。区别只发生在
motionName命中后：strict取得最终raw motion node，新建一个 `PSBValueDispatch`，再新建TJS Array，按
顺序push `[dispatch object closure, 实际命中的outer map key]`，返回Array Variant。没有命中则返回
Void，而不是空Array。

本地 `ResourceManager.cpp:591..660` 已准确保留dispatch/Array发布次序、实际map key、direct失败后的
full-scan，以及一个重要异常边：成功构造dispatch后，到第一个Array元素成功AddRef并commit之前没有
RAII owner；Array创建或第一个deque emplacement扩容抛异常会泄漏dispatch。第一个元素发布成功后，
Array开始拥有dispatch，后续第二元素或返回路径异常由Array Variant回收。本轮无需改运行时C++。

## 2. 四端主 callback

| 平台 | callback | 完整指令 | 独立cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6A72B4` | 791 | DWARF landing内联/尾部 |
| Android armv7 | `0x57B9F8` | 262 | 平台EH表驱动 |
| iOS arm64 | `0x100101E84` | 255 | 平台LSDA驱动 |
| iOS armv7 | `0xFF11C` | 396 | `0xFF514`，207条、35个SjLj状态 |

四个callback全部fresh decompile并完成full disassembly，无截断。iOS armv7 cleanup同样fresh完整读取；
它覆盖direct/fallback两套raw-node临时值、两个UTF-8 string、split vector、Array Variant和分配失败
状态。call-site 17..20明确对应Array创建、第一元素emplace、dispatch本地Release和第二元素emplace：
Array Variant一旦构造就会在这些异常路径析构，但没有独立scope guard对“尚未进入Array的dispatch”
执行Release。

## 3. 共同源码伪代码

```text
Variant ResourceManager::findMotion(Variant projectKey, ttstr path):
    pieces = split(path, "/")
    chara = toUtf8(pieces[1])
    motionName = toUtf8(pieces[2])

    if projectKey.type != Void:
        projectString = requireExactString(projectKey)
        direct = loadedModules.find(ttstr(projectString))
        if direct != end:
            motion = findRawMotionOrMiss(direct.value.file, chara, motionName)
            if motion.hit:
                dispatch = new PSBValueDispatch(motion.file, motion.node)
                array = createTJSArrayWithItems()
                array.items.emplace_back(dispatch, dispatch)
                dispatch.Release()
                array.items.emplace_back(direct.key)
                return array.value

    for entry in loadedModules:
        motion = findRawMotionOrMiss(entry.value.file, chara, motionName)
        if motion.hit:
            dispatch = new PSBValueDispatch(motion.file, motion.node)
            array = createTJSArrayWithItems()
            array.items.emplace_back(dispatch, dispatch)
            dispatch.Release()
            array.items.emplace_back(entry.key)
            return array.value

    return Void
```

`findRawMotionOrMiss` 只是说明重复的共同序列，不代表二进制有该独立函数；四端都保留两套展开：
`strict("object") -> contains(chara) -> strict(chara) -> strict("motion") -> contains(motionName) ->
strict(motionName)`。固定键或最终strict读取失败传播异常，只有dynamic contains miss进入下一候选。

## 4. 与 isExistMotion共享的输入和map边界

本轮复用并重新核对上一slice的完整helper族：

- split：Android arm64 `0x695114`、Android armv7 `0x571C50`、iOS arm64 `0x1000F52D0`、
  iOS armv7 `0xF1D20`；
- UTF-8 narrow：`0x5B5E7C` / `0x4EF09C` / `0x100049494` / `0xF0708`；
- strict getter：`0x599038` / `0x4DD49C` / `0x1000EDA48` / `0xE9D10`；
- contains：`0x5999B8` / `0x4DD918` / `0x1000EDEF0` / `0xEA120`；
- direct map find：`0x6E8CD4` / `0x5A7284` / `0x100139AA8` / `0x139CEC`。

所以findMotion继承全部尖锐输入行为：忽略 `pieces[0]` 和额外segments；少于3段时未检查
`pieces[1]/[2]`；Void只跳过direct而不保护短path；project非Void且非String抛转换异常；project key
不做placed-path normalization；direct普通miss后完整map从begin重扫且可能重复同一node。

## 5. 最终raw node与dispatch owner

motionName contains命中后，callback再次strict取得最终motion node。这个raw pair先保留PSB owner，再交给
`PSBValueDispatch` constructor：

| 平台 | dispatch ctor | 对象大小 |
|---|---:|---:|
| Android arm64 | `0x597EB4` | `0x30` |
| Android armv7 | `0x4DCB50` | `0x18` |
| iOS arm64 | `0x1000EC248` | `0x30` |
| iOS armv7 | `0xE8874` | `0x18` |

constructor复制/AddRef raw file owner、保存node pointer、refCount初始化为1、valid初始化为true。它不依赖
ResourceManager map node继续存在；因此返回Array被外部保留后，`unload`/`unloadAll`释放cache holder仍
不会使第0元素失效。Array第1元素的ttstr key也独立AddRef其字符串holder。

## 6. Array factory边界

四端使用同一 `createTJSArrayWithItems_guess` 源结构：

| 平台 | helper | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x702098` | 63 |
| Android armv7 | `0x5BAA70` | 60 |
| iOS arm64 | `0x10029FF58` | 49 |
| iOS armv7 | `0x2A4A80` | 91 |

factory调用 `TJSCreateArrayObject()`，把同一dispatch写为Object/ObjThis，Variant constructor对两者各
AddRef，然后Release factory初始引用。随后用Array class ID调用 `NativeInstanceSupport(GETINSTANCE)`；
只有status精确等于 `TJS_S_OK == 0` 才发布 `&tTJSArrayNI::Items`，任何非零status都把items设为null，
不读取输出slot。

findMotion没有检查items：Array对象为null会在factory内部的vtable调用形成尖锐失败；native-instance
status非零则后续 `items->emplace_back` 解引用null。这不是返回Void分支。本地helper保留相同行为。

## 7. `std::deque<tTJSVariant>` 两元素提交

| 操作 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Object emplace | 快路径内联；慢路径 `0x6E941C`，58条 | `0x57BD88`，35条 | `0x1001023D0`，58条 | `0xFF728`，65条 |
| map/deque grow或String emplace | String快/慢路径内联；map grow `0x53453C`，253条 | String `0x4EA126`，61条 | String `0x1001024C4`，49条 | String `0xFF7E0`，59条 |

所有列出的helper均fresh decompile/full disassembly。第一元素写type=Object，并对dispatch的Object与
ObjThis各AddRef；callback随后对本地ref执行一次Release，所以Array最终持有同一dispatch的两个closure
引用。第二元素写type=String并AddRef outer map key holder；不是把输入project Variant或查询path直接
塞进Array。

Android libstdc++以约512-byte block组织deque：arm64 `20-byte Variant * 25 = 500`，armv7
`12-byte Variant * 42 = 504`。iOS libc++以约4096-byte block组织：arm64每block 204个20-byte
Variant，armv7每block 341个12-byte Variant。map指针扩容、recentering和block大小是STL/ABI实现
细节；源码应继续使用 `std::deque<tTJSVariant>::emplace_back`。

## 8. 返回值的精确内容与identity

- direct hit返回 `[freshDispatch, direct->first]`；第二元素来自map key，即load时规范化并缓存的key。
  它按内容通常等于project String，但不共享输入Variant本身的owner身份。
- fallback hit返回 `[freshDispatch, entry.first]`，因此报告实际命中的module，而不是请求的缺失project。
- 每次findMotion命中都新建dispatch和Array；同一motion连续调用的Array identity、dispatch identity均
  不同，但raw PSB owner相同。
- Array长度精确为2，顺序固定；没有附加chara/motionName、命中序号或布尔标记。
- 未命中返回Void；empty map同样是Void，绝不返回 `[]` 或 `[null,key]`。

## 9. 异常发布阶段与泄漏边

命中后的owner状态机为：

```text
new dispatch succeeds:            dispatch ref=1, no RAII guard
Array factory succeeds:           Array Variant owns array; dispatch still unguarded
first deque emplace succeeds:     Array item owns Object+ObjThis dispatch refs
local dispatch.Release():         removes original ref
second deque emplace succeeds:    Array also owns copied map-key string
return Variant copy/move:          caller owns Array; local Array owner ends
```

具体边界：

- `operator new`失败：无dispatch对象；正常传播。
- constructor失败：遵循new-expression构造失败回收allocation；已构造raw holder成员按ctor规则清理。
- dispatch成功后，Array创建抛出：没有scope guard，dispatch ref=1泄漏。
- Array成功后，第一emplace若先为deque分配block/map并在分配中抛出：Array自身会析构，但尚未AddRef
  dispatch，故dispatch仍泄漏。
- 第一emplace完成后AddRef/commit已发布，callback才Release本地ref；之后第二emplace扩容抛出时，Array
  析构第0元素并释放dispatch，不泄漏。
- ttstr holder AddRef本身不分配；第二元素可能抛出的主要新增点是deque block/map增长。
- direct/fallback两套命中代码保持完全相同的发布次序和异常owner。

用 `unique_ptr` 从dispatch构造点开始保护会修复泄漏但改变参考边界，因此本地有意保留裸指针handoff。

## 10. raw导航、异常和并发

- strict固定键缺失、chara node缺固定motion、unknown内部tag、Variant类型错误、split/string/vector分配
  全部无catch传播；Void只表示普通dynamic miss或map耗尽。
- 取得final motion node后才分配dispatch/Array，普通contains miss不会创建script object。
- function为const但map和raw owner无锁；与load/unload/unloadAll并发形成container/owner data race。
- direct失败后的full scan仍可能重新检查direct node；若损坏module先抛，unordered-map平台迭代顺序会
  影响异常先后，这不是可移植排序承诺。
- 函数不修改map、spec、SourceCache、RandomGenerator或纹理map，也不记录日志。

## 11. 本地逐行对照与验证

`cpp/plugins/motionplayer/ResourceManager.cpp:591..660` 已逐项匹配：两段重复查找、final strict getter、
fresh dispatch、Array factory、first emplace、Release、actual key second emplace和Void miss都与四端一致。
`cpp/plugins/motionplayer/RuntimeSupport.cpp:450` 也保持factory closure/NativeInstanceSupport exact-zero与
borrowed Items pointer边界；无需C++修正。

`tests/unit-tests/plugins/motionplayer-dll.cpp:27126` 的真实PSB fixture验证direct/fallback结果均为两元素
Array、第0元素为Object、第1元素为实际path、两次Array identity不同、missing返回Void、Integer project
抛异常；同一测试随后unload并验证外部raw dispatch owner继续有效。

本轮完成四端主callback、Array factory、deque object/string emplace/grow和iOS armv7 35-state cleanup
的fresh decompile/full disassembly；四端IDB已命名、注释、书签并原位保存；NCB确定性重生成、严格TSV
字段和 `git diff --check` 已通过。当前缺少CMake/Ninja/Emscripten与完整依赖头，不能宣称正式
unit/Web build；工具链恢复后应运行现有fixture及Array-native失败/allocator fault injection测试。
