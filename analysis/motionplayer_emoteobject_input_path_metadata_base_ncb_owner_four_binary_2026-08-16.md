# EmoteObject input-path / metadata-base ncb owner 管线四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面从 `reference/binaries/` 四份当前发布物重新提取 `EmoteObject` constructor/init 的
完整 load尾部，专门闭合下列此前只写出值语义、没有写出source identity的边界：

- 成员 `modulePaths` copy与后续load loop究竟读取成员还是caller输入；
- 多个ResourceManager load result如何复用同一个Variant工作槽；
- `metadata`、`base`与最后load-result owner是否同时存在；
- 哪一层使用显式 `ncbPropAccessor`，以及 `chara/motion` 的typed getter和hint身份；
- Player project/chara/forced-motion/applyMetadata顺序；
- 正常与异常退出时脚本owner和已发布raw heap owner的释放边界。

四端共同确认当前portable存在三个残留偏差：

1. native先copy input paths到成员，但load loop仍遍历caller的input vector；portable错误遍历
   `_modulePaths`成员副本；
2. Player project取caller `modulePaths.back()`；portable错误取`_modulePaths.back()`；
3. native最后load-result工作Variant在读取`base`时被覆盖，之后只有metadata与base工作槽两个
   persistent Variant；portable另声明独立base，使dead loaded owner多活到构造尾。

此外，四端都只有base层建立一个显式copied/forced/retained `ncbPropAccessor`。`chara`、
`motion`是两次flags=0 typed string getter，使用两个constructor-private非null hint；metadata与
base的前两次Variant getter则为flags=0、hint=null，并由各自Variant receiver/objthis直接执行。

## 四端函数映射

| 参考 | constructor/init入口 | IDA大小 | 正常析构 |
|---|---:|---:|---:|
| Android arm64 | `0x67AF8C` | `0x660` | `0x67C800` |
| Android armv7 | `0x5604B8` | `0x294` | `0x5610BE` |
| iOS arm64 | `0x1001B4984` | `0x398` | `0x1001B5058` |
| iOS armv7 | `0x1B4500` | `0x39E` | `0x1B4CCE` |

函数名来自recovery语义命名；stripped二进制不能证明原始标识符，所以继续保留
`EmoteObject_init_guess`与其余 `_guess` 名。

对象仍是既有纵切面确认的自然三成员布局：

```text
ResourceManager *rawOwner
EmoteEngine *rawOwner
vector<ttstr> modulePaths
```

64位为40B，32位为20B；V146没有改变这项ABI结论。

## 关键调用位置

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| member path copy | `0x67B08C` | `0x560552` | `0x1001B4A7C` | `0x1B4624` |
| load caller-input path | `0x67B0C4` | `0x560590` | `0x1001B4AB8` | `0x1B466A` |
| last-result working-slot assign | `0x67B0D0` | `0x560598` | `0x1001B4AC4` | `0x1B4676` |
| `metadata` getter | `0x67B13C` | `0x5605DE` | `0x1001B4B2C` | `0x1B46CC` |
| `base` getter / working-slot overwrite | `0x67B188` | `0x560606` | `0x1001B4B78` | `0x1B4704` |
| copied base accessor | `0x67B1A4` | `0x560614` | `0x1001B4B94` | `0x1B471A` |
| accessor conversion Variant dtor | `0x67B1F8` | `0x56062A` | `0x1001B4BB8` | `0x1B473C` |
| typed `chara` | `0x67B228` | `0x560642` | `0x1001B4BDC` | `0x1B4764` |
| typed `motion` | `0x67B274` | `0x560658` | `0x1001B4C00` | `0x1B478C` |
| caller input `back()` | `0x67B298` | `0x560662` | `0x1001B4C0C` | `0x1B479E` |
| project commit | `0x67B2E0` | `0x560682` | `0x1001B4C2C` | `0x1B47BE` |
| set chara | `0x67B31C` | `0x5606AE` | `0x1001B4C5C` | `0x1B47F0` |
| force play | `0x67B358` | `0x5606E0` | `0x1001B4C94` | `0x1B4828` |
| apply metadata | `0x67B380` | `0x5606F8` | `0x1001B4CB8` | `0x1B484C` |

四端 member-copy之后的loop begin/end都重新从constructor参数读取，而不是从self的
member header读取。project同样从参数end减一个element取得。空输入没有guard：没有load时
working Variant仍为Void，metadata getter先失败/抛出；若执行到project，`back()`本身也是
unchecked边界。portable不加入空vector fallback。

## Variant工作槽与source tree

四端可见的source tree不是三个平行persistent Variant：

```text
caller modulePaths
├─ copy -> EmoteObject.modulePaths                    persistent member
├─ iterate each original input element -> RM.load
└─ back() -> Player project argument

working Variant
├─ load #0 result
├─ copy-assign load #1 ... load #N result             only last survives
├─ receiver for `metadata`
└─ overwritten by `base`                              final-load owner ends here

metadata Variant                                      independent, survives apply
└─ receiver for `base`

base working Variant
└─ copied/forced base ncbPropAccessor
   ├─ typed chara ttstr
   └─ typed motion ttstr
```

member copy发生在第一次load前，但这不授权后续loop改读member。input与member的ttstr通常
共享引用计数字符串内容，所以普通值测试很难区分；四端寄存器/stack来源明确区分了两个
vector header。

最后load-result owner在base getter写回working槽时被替换。metadata独立持有其property
object并一直活过project、setChara、force play与`applyMetadata(metadata)`。base working
Variant也活到constructor尾；显式base accessor来自它的second copy，转换Variant在第一次
typed getter前销毁，而accessor继续保活dispatch。

## hint身份

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `chara` | `0x1AB5028` | `0x11115C0` | `0x101B6A0D8` | `0x187DAF8` |
| `motion` | `0x1AB502C` | `0x11115C4` | `0x101B6A0DC` | `0x187DAFC` |

data xref显示两槽彼此相邻但独立，并且只由本constructor使用；Android armv7额外显示的尾部
引用落在函数外literal/data区域，不形成第二个语义caller。四库现将它们命名为
`EmoteObject_baseCharaHint_guess`与`EmoteObject_baseMotionHint_guess`。

metadata/base getter都传null hint；不能为了统一API给它们添加缓存槽。base两次typed getter
都以base accessor dispatch自身作为receiver和objthis，ordinary HRESULT不作为caller gate。

## 精确共同伪代码

```cpp
EmoteObject::EmoteObject(const vector<ttstr> &inputPaths) {
    Variant kag = eval(L"global.kag");
    rm = new ResourceManager(kag, 20_MiB);

    {
        Variant rmAdaptor = makeStickyAdaptor(rm);
        engine = new EmoteEngine(rmAdaptor);
    }

    memberPaths = inputPaths;

    Variant working;
    for (const ttstr &path : inputPaths)
        working = rm->load(path);

    const Variant metadata = propGet(working, L"metadata", 0, nullptr);
    working = propGet(metadata, L"base", 0, nullptr);

    ncbPropAccessor base{Variant(working)};
    const ttstr chara = base.GetValue(
        L"chara", Tag<ttstr>(), 0, &charaHint);
    const ttstr motion = base.GetValue(
        L"motion", Tag<ttstr>(), 0, &motionHint);

    Player &player = engine->player();
    player.setProject(Variant(inputPaths.back()));
    player.setChara(chara);
    player.play(Force, motion);
    engine->applyMetadata(Variant(metadata));
}
```

这里的伪代码只为表达共同source shape；`propGet`是语义占位，不能由stripped发布物恢复原始
helper拼写。portable继续使用已经四端审计过的普通getter helper处理前两次borrowed receiver，
只把四端明确存在的base层恢复成显式`ncbPropAccessor`。

## 正常teardown

applyMetadata返回后的共同释放顺序：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| motion ttstr | `0x67B394` | `0x560704` | `0x1001B4CC8` | `0x1B4858` |
| chara ttstr | `0x67B3A0` | `0x56070A` | `0x1001B4CD0` | `0x1B485E` |
| base accessor | `0x67B3BC` | `0x560722` | `0x1001B4CEC` | `0x1B4872` |
| metadata Variant | `0x67B3C4` | `0x560726` | `0x1001B4CF4` | `0x1B4876` |
| base working Variant | `0x67B3CC` | `0x56072C` | `0x1001B4CFC` | `0x1B487C` |

这正好对应portable现在的声明顺序：working、metadata、base accessor、chara、motion；逆序
析构得到上表。旧代码另有独立base局部，因此析构尾会形成base/metadata/loaded三个Variant，
并把last-load dispatch多保留一段。

`applyMetadata`接收metadata的by-value copy；其参数Variant在callee返回处先销毁，然后才开始
上表caller locals teardown。调用时metadata、base accessor与base working Variant全部仍活着。

## HRESULT、重入与渐进提交

- metadata/base都是flags=0 getter，hint=null，receiver与objthis相同；普通失败HRESULT不被
  caller用作branch，只要getter写出usable Variant，后续转换/读取继续。
- chara/motion typed getter同样忽略普通HRESULT后转换写出值。若getter写值后返回失败，值仍被
  使用；转换抛异常才展开构造函数。
- chara getter重入即使清除base working Variant，retained base accessor仍使motion getter可用。
- chara getter抛出时，project尚未提交；motion getter抛出时，project与chara仍尚未提交。
- project提交后发生的setChara/play/applyMetadata异常不会rollback已写Player状态。
- member path vector在任何load/property/Player/apply异常前已经copy完成；异常会析构该member
  vector，但不会回收此前已发布的Engine/RM raw owner。

最后一项承接既有raw-owner纵切面：pending `new` allocation失败路径会delete pending storage；
一旦RM/Engine pointer写进EmoteObject slot，后续异常只展开stack owners与paths member，两个
heap owner仍泄漏。恢复base accessor不能用额外RAII修复这项发布物边界。

## portable源码与回归

`cpp/plugins/motionplayer/EmotePlayer.cpp` 已改为：

- member copy保持原位，但load loop和project back改读constructor input；
- 用base覆盖`loaded`工作Variant，删除第三个持久Variant owner；
- 新建唯一base `ncbPropAccessor`；
- `chara/motion`改为两个typed `GetValue<ttstr>`并使用独立private hints；
- 局部声明顺序直接表达native teardown。

已有真实PSB fixture测试先从同一module提取expected metadata/base/chara/motion，再经
`D3DEmotePlayer::load -> new EmoteObject`生产链构造对象，并新增断言内部Player收到完全相同的
chara与motion。该用例覆盖production constructor尾部；source owner与工作槽覆盖仍以四端
静态指令顺序为决定性证据。

当前Web preset显式关闭`ENABLE_TESTS`，所以新增断言由普通/headless完整test TU做编译/类型
检查，没有在Web增量构建中运行。Windows native测试仍受既有cocos2dx vcpkg失败阻断；本页
不把syntax-only写成runtime pass。

## IDB落地

四个recovery IDB均完成：

- 两个private hint data符号 `_guess` 命名；
- constructor函数级V146 owner/source注释1条；
- input/member/work-slot/accessor/Player-seed/teardown逐地址注释19条；
- `V146 EmoteObject input path + metadata/base ncb owner pipeline` bookmark；
- force-recompile后constructor重新反编译成功；
- 新hint名均在新反编译文本中回读；
- `search_text(..., include=comments)`每库回读19/19；
- 四份数据库最终原位保存。

## 验证

- 普通完整test TU `-fsyntax-only`通过，仅有既有 `_tss` warning；
- headless完整test TU `-fsyntax-only`通过，仅有同一warning；
- Web Debug增量构建 `3/3`，最终wasm链接成功；
- Wasmtime Headless Debug增量构建 `4/4`，两个EmotePlayer对象与最终wasm链接成功；
- Web `index.wasm`（85,637,971 bytes）与Wasmtime `index.wasm`（84,985,117 bytes）均由
  Node `WebAssembly.Module`成功解析；
- 定向源码审计确认load/back读取input、last-load槽覆盖、1个base accessor、2个typed getter、
  2个独立hint，chara/motion raw wrapper为0；
- `git diff --check`通过；新增文档也经独立行尾空白审计，无whitespace error。

本纵切面闭合的是EmoteObject constructor的input-path、metadata/base source identity与stack
owner tree；对象三成员ABI、raw-owner正常析构和constructor-failure leak矩阵继续由既有纵切面
约束。这不表示整个motionplayer已经100%复原。
