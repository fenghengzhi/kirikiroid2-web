# MotionPlayer parameter parse/append nested ncb accessor 与 strict division 四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面重新从 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前发布物提取 `Player_appendParameterEntry_guess` 与
`Player_parseParameterList_guess`。既有 recovery IDB 注释只用于定位，行为结论来自本轮重新
取得的反编译、逐指令顺序和数据交叉引用。

既有 parameter 文档正确记录了 vector 渐进提交、字段顺序、缺失 `division` 的差值回退和
finalize；但 portable 源码仍通过通用 property wrapper 读取参数，因而没有表达发布物真正的
source identity、receiver owner、typed ncbind 模板和 HRESULT 边界。V145 已闭合为：

- append 只拒绝非 Object；先复制/强制/保活 item `ncbPropAccessor`，再默认追加 vector 记录；
- `id/discretization/rangeBegin/rangeEnd` 是四次顺序固定、flags=0 的 typed `GetValue`；
- `id` 复用 `Player_getCommandList_guess` 的同一个静态 hint 槽，另外三个字段各有独立 hint；
- optional `division` 不是 `HasValue + GetValue`，而是一次
  `checkVariant(MEMBERMUSTEXIST, hint=null)`；
- strict probe 的 HRESULT 单独控制 present/missing 分支，即使失败前写出 Variant也走 missing；
- parse 只拒绝 Void；复制/保活 root accessor，Count快照一次，逐项 typed numeric getter；
- 每个 item Variant在 append返回后立即析构；finalize在 root accessor仍存活时运行，root最后释放。

stripped 发布物不能证明原始 C++ 标识符，因此所有恢复函数名继续保留 `_guess`。

## 四端函数映射

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_appendParameterEntry_guess` | `0x6AEAF8` (`0x3A4`) | `0x57FA14` (`0x19A`) | `0x100106D00` (`0x1F8`) | `0x104168` (`0x26E`) |
| `Player_parseParameterList_guess` | `0x6AF40C` (`0x1BC`) | `0x57FFE8` (`0xB0`) | `0x100107370` (`0xE0`) | `0x1048FC` (`0x102`) |

括号内为本轮 IDA 函数大小。四端的 STL、异常和指令编码不同，但 source-level owner 树与
读取/释放顺序一致。

## append 的关键位置

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Object gate | `0x6AEB2C` | `0x57FA2E` | `0x100106D20` | `0x1041B6` |
| copied item accessor | `0x6AEB34` | `0x57FA34` | `0x100106D28` | `0x1041C6` |
| conversion Variant dtor | `0x6AEB88` | `0x57FA48` | `0x100106D4C` | `0x1041E6` |
| vector default append | `0x6AEB8C` | `0x57FA4C` | `0x100106D50` | `0x1041EC` |
| typed `id` | `0x6AEBE4` | `0x57FA88` | `0x100106DA8` | `0x104254` |
| typed `discretization` | `0x6AEC48` | `0x57FADA` | `0x100106E04` | `0x1042CC` |
| typed `rangeBegin` | `0x6AEC68` | `0x57FAF6` | `0x100106E24` | `0x1042FA` |
| typed `rangeEnd` | `0x6AEC8C` | `0x57FB14` | `0x100106E48` | `0x104314` |
| direct strict `division` probe | `0x6AECBC` | `0x57FB32` | `0x100106E7C` | `0x104340` |
| missing division derive | `0x6AED10` | `0x57FB48` | `0x100106E94` | `0x104362` |
| initial value read | `0x6AED4C` | `0x57FB66` | `0x100106EB4` | `0x10438C` |
| normalize | `0x6AEDC4` | `0x57FB74` | `0x100106EBC` | `0x10439E` |
| division scratch dtor | `0x6AEDC8` | `0x57FB7A` | `0x100106EC4` | `0x1043A4` |
| item accessor release | `0x6AEDD8` | `0x57FB8C` | `0x100106ED4` | `0x1043AE` |

item accessor construction明确早于 vector growth/default construction；后者明确早于第一项
`id` 读取。因此脚本 getter重入时，vector中已经存在部分记录，而 item dispatch由 accessor
保活，不能用一次性的 borrowed raw dispatch 模拟。

## parse 的关键位置

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Void gate | `0x6AF438` | `0x580000` | `0x10010738C` | `0x104948` |
| copied root accessor | `0x6AF440` | `0x580004` | `0x100107394` | `0x104956` |
| conversion Variant dtor | `0x6AF49C` | `0x580018` | `0x1001073B8` | `0x104976` |
| Count snapshot | `0x6AF4A8` | `0x58001E` | `0x1001073C4` | `0x104980` |
| typed indexed Variant | `0x6AF4D8` | `0x580044` | `0x1001073E8` | `0x10499E` |
| append item | `0x6AF4F8` | `0x58004C` | `0x1001073F4` | `0x1049AA` |
| item Variant dtor | `0x6AF500` | `0x580052` | `0x1001073FC` | `0x1049B0` |
| finalize | `0x6AF514` | `0x58005E` | `0x100107410` | `0x1049C4` |
| root accessor release | `0x6AF524` | `0x580070` | `0x100107420` | `0x1049CE` |

Android arm64把部分 typed helper内联为 dispatch虚调用；其余端保留更显式的共享 helper。
这不改变共同结构：root转换 temporary在 Count前析构，但 retained accessor跨 Count、整个
indexed loop和finalize存活。

## hint 身份

四个 typed named getter都传非 null hint，但不是同一槽：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `id` | `0x1AB53DC` | `0x1111878` | `0x101B698A4` | `0x187D548` |
| `discretization` | `0x1AB53E0` | `0x111187C` | `0x101B698A8` | `0x187D54C` |
| `rangeBegin` | `0x1AB53E4` | `0x1111880` | `0x101B698AC` | `0x187D550` |
| `rangeEnd` | `0x1AB53E8` | `0x1111884` | `0x101B698B0` | `0x187D554` |

四端 data xref共同显示 `id` 槽同时被 `Player_getCommandList_guess` 使用。本地因此复用
`detail::commandIdMemberHint_guess`，而不是为 parameter parser再造一个同名但不同身份的
静态缓存。另三个字段在 `PlayerVariable.cpp` 中各保留独立 `_guess` hint。

`division` 明确传 null hint。把它改成普通 hinted getter、两次访问或与四个 required字段
共享 hint都会改变 native 可观察的 dispatch 参数与重入次数。

## source owner 树与 teardown

```text
parse input Variant
└─ copied/forced root ncbPropAccessor                 survives finalize
   └─ indexed item Variant                            one loop iteration
      └─ append(const Variant&)
         └─ copied/forced item ncbPropAccessor         survives all five reads
            ├─ four typed field-result temporaries
            └─ division Variant scratch                destroyed before item accessor

per iteration: append returns -> indexed item Variant dies
after loop: finalizeParameterTable -> root accessor dies
```

parse root与append item是两层独立 owner。indexed getter返回的 Variant只需活到当前 append
返回；append自行建立第二层 retained owner，所以 getter重入清除root内部保存的item也不会让
四个字段读取悬空。相反，ramp map只借用新 vector entry指针，不保留脚本 item dispatch；item
可在当前 iteration末尾死亡，而root仍必须存活到finalize之后。

正常 append局部释放顺序为：typed字段临时量随赋值结束释放，division scratch在函数尾先
析构，item accessor最后 Release。异常路径由对应平台的 EH/SjLj cleanup保持已构造局部量的
逆序释放；已 default-append 的 vector记录没有 rollback。

## 精确共同伪代码

```cpp
bool parseParameterList(const Variant &parameters) {
    if (parameters.Type() == Void)
        return false;

    ncbPropAccessor root{Variant(parameters)};
    const int count = root.GetArrayCount();
    for (int index = 0; index < count; ++index) {
        const Variant item = root.GetValue(index, Tag<Variant>(), 0);
        appendParameterEntry(item);
    }
    finalizeParameterTable();       // root仍然存活
    return true;
}

void appendParameterEntry(const Variant &parameter) {
    if (parameter.Type() != Object)
        return;

    ncbPropAccessor item{Variant(parameter)};
    entries.emplace_back();         // 第一次脚本getter之前已提交
    Entry &entry = entries.back();
    entry.division = 0.0;

    entry.id = item.GetValue(L"id", Tag<ttstr>(), 0, sharedCommandIdHint);
    entry.discretization = item.GetValue(
        L"discretization", Tag<bool>(), 0, discretizationHint);
    entry.rangeBegin = item.GetValue(
        L"rangeBegin", Tag<real>(), 0, rangeBeginHint);
    entry.rangeEnd = item.GetValue(
        L"rangeEnd", Tag<real>(), 0, rangeEndHint);

    Variant divisionScratch;
    double division;
    if (item.checkVariant(L"division", divisionScratch)) {
        division = divisionScratch.AsReal();
    } else {
        division = entry.rangeEnd - entry.rangeBegin;
        if (division <= 0.0)
            division = 1.0;
    }
    entry.division = division;
    normalize(entry, readInitialParameterValue(entry.id));
}
```

这里的 accessor构造表达的是 observable owner/source shape，不代表 stripped二进制可恢复原始
局部变量拼写。

## HRESULT 与边界行为

四个 required字段、Count和indexed getter都遵循普通 typed ncbind边界：dispatch可先写出
usable Variant再返回失败 HRESULT；模板仍转换/复制写出值，caller不以该 HRESULT分支。
转换本身或 dispatch抛异常才中止，且保留此前渐进提交。

`division` 是例外：`checkVariant` 直接以 `TJS_MEMBERMUSTEXIST` 调用 PropGet，返回成功与否
控制分支。若 dispatch写出 real 99后返回 `TJS_E_FAIL`：

- scratch仍持有写出的 99，直到函数尾正常析构；
- caller不调用 `AsReal`，而是进入missing分支；
- missing分支使用 `rangeEnd-rangeBegin`；
- 只有该推导值的有序 `<=0` 才替换为1；NaN不会触发fallback；
- 显式成功取得的division不执行任何 `<=0`/NaN/infinity修正。

Count只快照一次，loop bound不会因脚本重入更改。indexed getter按升序按需执行；任一后续
异常不回滚已追加记录，也不移除已完成的ramp节点。parse的非Void非Object输入仍尝试作为
array-like source读取Count；这与append的Object-only element gate是两个不同层级。

## portable源码与回归探针

`cpp/plugins/motionplayer/PlayerVariable.cpp` 已改为：

- append显式一个 item `ncbPropAccessor`、四个 typed `GetValue`和一个 `checkVariant`；
- parse显式一个 root `ncbPropAccessor`、一次 `GetArrayCount`和一次循环内typed indexed
  `GetValue`；
- 两函数内旧 `detail::motionPropGet*` / `detail::motionTryPropGet` 调用均为0；
- accessor构造、vector append、field read、finalize和owner teardown按native scope排序。

两个 Catch2 probe覆盖原先 wrapper无法锁定的边界：

1. `parameter append uses a retained typed ncb source and strict division probe`
   - 第一次 `id` getter清除调用者最后一份item owner，后续读取仍成功；
   - 每次getter都观察到vector中已经有一条记录；
   - required getter均先写值再返回失败HRESULT；
   - 四个flags/hint/objthis和共享 `id` hint身份精确匹配；
   - strict division写99后返回失败，最终使用差值4并把raw 0归一化为2。
2. `parameter parse retains its root through append and finalization`
   - Count和indexed getter都先写值再失败；Count只读一次；
   - indexed getter清除root storage与调用者最后一份root owner；
   - item accessor使脚本item跨五次读取存活，随后item先于root析构；
   - root析构时观察到ramp map已经有一项，证明finalize发生在root释放之前。

Web preset显式为 `ENABLE_TESTS=false`，所以这两条 probe由普通与headless完整test TU response
file执行编译/类型检查，没有在两套Web增量构建中运行。既有Windows native测试配置仍被
cocos2dx vcpkg构建失败阻断；本页不把syntax-only通过误写成runtime test通过。

## IDB落地

四个 recovery IDB 均完成：

- append与parse各1条V145函数级注释；
- 两函数合计23条逐指令owner/getter/hint/strict-probe/finalize/teardown注释；
- `V145 parameter append ncb lifetime + strict division` 与
  `V145 parameter parse root lifetime + finalize order` 两个bookmark；
- 两函数force-recompile后均重新反编译成功；
- `search_text(..., include=comments)` 在每库回读23/23逐指令注释；
- 四份数据库最终原位保存。

没有把共享模板或内联叶子错误重命名成parameter专属函数；函数名仍保留 `_guess`。

## 验证

- 普通完整test TU `-fsyntax-only`通过，仅有仓库既有 `_tss`弃用warning；
- `KRKR2_WASMTIME_HEADLESS=1` 完整test TU `-fsyntax-only`通过，仅有同一warning；
- Web Debug完整增量构建 `33/33`，最终 `index.wasm`链接成功；
- Wasmtime Headless Debug完整增量构建 `62/62`，最终wasm链接成功；
- Web `index.wasm`（85,637,923 bytes）与Wasmtime `index.wasm`（84,985,069 bytes）均由
  Node `WebAssembly.Module`成功解析；
- 定向源码审计确认append为1 accessor/4 typed getter/1 strict check，parse为
  1 accessor/1 Count/1 indexed getter，旧raw helper为0；
- `git diff --check`通过；仅有工作树既有LF/CRLF转换提示，没有whitespace error。

本页只闭合parameter parse/append的typed source identity、hint、owner与strict division边界；
parameter entry ABI、初值父链、normalize数值语义、ramp multimap及析构purge仍以各自四端纵切面
为准。这不表示整个motionplayer已经100%复原。
