# MotionPlayer buildVariableList nested ncb accessor/source identity 四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面重新从四份当前 `reference/binaries/` 提取
`EmoteEngine_buildVariableList_guess` 的完整反编译和调用顺序，不把 2026-08-15 的既有
IDB 注释当作行为证据。目标是闭合 variable metadata builder 的真实 C++ source shape：

- 哪些脚本对象由 `ncbPropAccessor` 保活；
- 哪些 getter 是 typed `GetValue`，以及普通失败 HRESULT 是否参与控制流；
- frame Dictionary 的 strict probe、第二次读取与 candidate Array 生命周期；
- `_variableRanges`、`_variableLabels`、`_variableFrameLists` 的渐进提交顺序；
- 每层 source Variant 与 accessor 的释放顺序。

四端共同证据确认：旧文档记录的总体容器/候选流水线正确，但 portable 源码仍把本函数的
Count、indexed/named getter包在 `detail::motionPropGet*` 中。那些 wrapper 在常规值路径上很
接近 native，却没有在源码层表达发布物的 typed ncb 调用、receiver owner identity和
failure-after-write 边界。V144 已将本函数完整改回直接 `ncbPropAccessor` 形状，并增加四层
owner probe与重复 label/candidate probe。

## 四端函数映射

| 参考 | 入口 | IDA 大小 |
|---|---:|---:|
| Android arm64 | `0x667910` | `0xAE8` |
| Android armv7 | `0x555FC0` | `0x3FE` |
| iOS arm64 | `0x1001A73C0` | `0x72C` |
| iOS armv7 | `0x1A693C` | `0x622` |

关键成员偏移仍为：

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| current `_variableLabels` | `+1228` | `+652` | `+860` | `+464` |
| `_variableFrameLists` | `+1248` | `+664` | `+880` | `+476` |
| HM5 `_variableRanges` | `+1328` | `+704` | `+944` | `+508` |

stripped 发布物不能证明原始 C++ 标识符，因此 builder与内部恢复名继续保留 `_guess`。

## 新鲜关键调用位置

下表只列控制 source shape 的位置；STL rehash/deque grow、ttstr hash与异常 landing pad仍保留
在各平台 recovery IDB 中。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| fresh labels Array | `0x667948` | `0x555FDE` | `0x1001A73EC` | `0x1A6960` |
| fresh frame Dictionary | `0x66795C` | `0x555FEE` | `0x1001A7404` | `0x1A69A8` |
| root Count | `0x667A74` | `0x55604A` | `0x1001A7488` | `0x1A6A22` |
| indexed item Variant | `0x667AB8` | `0x5560DA` | `0x1001A74F0` | `0x1A6A60` |
| typed label string | `0x667B40` | `0x556108` | `0x1001A7530` | `0x1A6A9E` |
| eager candidate Array | `0x667C60` | `0x55616E` | `0x1001A77AC` | `0x1A6CB6` |
| strict `HasValue` | `0x667CE4` | `0x5561B2` | `0x1001A7810` | `0x1A6D02` |
| hit second getter | `0x667DA4` | `0x5561F8` | `0x1001A7884` | `0x1A6D58` |
| miss label append | `0x667D10` | `0x5561CE` | `0x1001A7838` | `0x1A6D24` |
| miss `MEMBERENSURE` set | `0x667EF0` | `0x556256` | `0x1001A791C` | `0x1A6DCA` |
| typed frameList Variant | `0x667F28` | `0x55626E` | `0x1001A7940` | `0x1A6DF2` |
| frameList Count | `0x667F98` | `0x556286` | `0x1001A7964` | `0x1A6E16` |
| indexed raw frame | `0x667FD0` | `0x55629E` | `0x1001A7994` | `0x1A6E32` |
| typed frame real | `0x66805C` | `0x5562D0` | `0x1001A79D4` | `0x1A6E7C` |
| raw frame Array copy | `0x668094` | `0x55630C` | `0x1001A7A0C` | `0x1A6EBA` |

Android arm64 把 indexed Variant、named label与 strict Dictionary probe的部分 ncb 模板
内联为 dispatch虚调用；另外三端保留共享 typed helper。A64 `0x683AD0` 仍是 V141 已确认的
24-caller Variant→ttstr转换/refcount叶子，不能改名为本 builder专属函数。平台 codegen差异
不改变共同源码结构。

## 完整 owner 树

本函数同时存在一个输出 Dictionary accessor和四层输入 source accessor：

```text
fresh labels Array
└─ Engine._variableLabels                         owning Variant

fresh frame Dictionary
├─ Engine._variableFrameLists                    owning Variant
└─ frameDictionary accessor                      function-wide receiver owner
   └─ label -> selected script Array property

copied input variableList
└─ variableListObject accessor                   function-wide receiver owner
   └─ indexed item Variant temporary
      └─ item accessor                           one outer iteration
         └─ frameList typed Variant temporary
            └─ frameList accessor                one outer iteration
               └─ indexed original frame Variant
                  ├─ frameObject second-copy accessor
                  └─ selected script Array copy  survives the iteration
```

根输入和新 Dictionary都先 copy Variant、强制 Object、构造 accessor，再清除转换 Variant；
两个 accessor跨完整循环存活。item和frameList同样在第一次成员/Count读取前清除转换
Variant。frame稍有不同：indexed getter返回的 original Variant必须继续存活，供
`frameArray->push_back(frame)`复制完整 closure/type；frameObject accessor来自它的第二份
copy，只负责读取实数 `frame` 字段。

正常局部释放顺序是：

```text
每个 frame: frameObject accessor -> original frame Variant
每个 item : frameList accessor -> label ttstr -> item accessor
函数末尾 : frameDictionary accessor -> variableListObject accessor
```

已发布的 Engine labels/Dictionary以及 Dictionary内的 script Array继续持有结果。输入
root、item和frameList不被结果容器保留；raw frame则被结果 Array保留。

## 精确共同伪代码

```cpp
working = createTJSArrayWithItems();
engine.variableLabels = working.value;
labelsItems = working.items;                     // borrowed native Items

engine.variableFrameLists = createDictionary();

ncbPropAccessor input{copied-and-forced(variableList)};
ncbPropAccessor frames{copied-and-forced(engine.variableFrameLists)};
const int count = input.GetArrayCount();

for (int i = 0; i < count; ++i) {
    Variant itemSource = input.GetValue(i, Tag<Variant>(), 0);
    ncbPropAccessor item{forced-second-owner(itemSource)};
    ttstr label = item.GetValue(L"label", Tag<ttstr>(), 0,
                                engineLabelHint);

    Range &range = engine.variableRanges.try_emplace(label, label).first->second;

    candidate = createTJSArrayWithItems();        // unconditional/eager
    working.value = candidate.value;
    frameItems = candidate.items;

    if (frames.HasValue(label, label.GetHint())) {
        working.value = frames.GetValue(label, Tag<Variant>(), 0,
                                        label.GetHint());
        frameItems = nativeArray(working.value).Items;
    } else {
        labelsItems.emplace_back(label);
        frames.SetValue(label, working.value, MEMBERENSURE,
                        label.GetHint());
    }

    ncbPropAccessor frameList{
        item.GetValue(L"frameList", Tag<Variant>(), 0,
                      engineFrameListHint)};
    const int frameCount = frameList.GetArrayCount();
    for (int j = 0; j < frameCount; ++j) {
        const Variant frame = frameList.GetValue(j, Tag<Variant>(), 0);
        ncbPropAccessor frameObject{Variant(frame)};
        const real value = frameObject.GetValue(
            L"frame", Tag<real>(), 0, controllerFrameHint);

        range.frameMin = range.frameMin < value ? range.frameMin : value;
        range.frameMax = value < range.frameMax ? range.frameMax : value;
        frameItems.push_back(frame);
    }
}
```

portable V144 对应的定向统计为：5 个显式 `ncbPropAccessor`、2 次
`GetArrayCount`、6 次 typed `GetValue`、1 次 `HasValue`、1 次 `SetValue`，本函数内旧
`detail::motionPropGet*` 调用为 0。

## Dictionary strict probe与candidate边界

HM5 range lookup/try-emplace发生在 candidate创建之前。新 range node构造成功后，不论
frame Dictionary是否已有同名属性，都会创建 fresh candidate Array。随后：

1. `HasValue` 产生一个独立 scratch Variant，以 `TJS_MEMBERMUSTEXIST`、动态 label hint和
   Dictionary自身作为 receiver/objthis执行 PropGet；scratch在分支前析构；
2. hit不复用scratch，而是再次以 flags=0读取 Variant；成功写入 `working.value` 时 eager
   candidate被释放，再严格取得返回对象的 native Array Items；
3. miss先把 label复制进最初 labels Array，再以 `TJS_MEMBERENSURE` 发布candidate；setter
   bool/HRESULT被忽略。

因此重复 label每次仍产生 candidate allocation/destruction和两个 Dictionary getter。hit的
第二次getter可以返回与strict probe不同的值；错误类型在 native Array转换处失败。miss的
PropSet普通失败不会撤销已追加label，inner frame loop仍向由 `working.value` 持有的candidate
写帧，只是 Dictionary可能没有对应属性。

## HRESULT、重入与渐进提交

两次 Count以及 indexed/named typed getter都遵循 ncb模板边界：初始化临时 Variant，调用
PropGet/PropGetByNum，不以 HRESULT作 gate，从写出的 Variant转换目标，再析构临时量。getter
若写出可用值后返回 `TJS_E_FAIL`，builder照常继续；getter或转换直接抛异常才展开当前 RAII
owner。

`HasValue` 是唯一以 HRESULT控制分支的 getter。它即使写出对象，只要 HRESULT为失败就走
miss；scratch对象仍在分支前析构。`SetValue`的返回 bool被调用者丢弃。

渐进提交没有 rollback：

- fresh labels和fresh Dictionary在第一次输入读取前已替换成员；
- range node在candidate创建前已插入；
- miss label在Dictionary set前已追加；
- 每帧 extrema在Array push前已更新；
- 任一后续异常只释放当前局部owner，保留此前成员、node、label、extrema和frame前缀。

extrema继续保留四端已确认的operand identity：相等或unordered时选择新frame值，因此
signed-zero和NaN结果不同于常见 `std::min/std::max` 写法。

## portable回归探针

`variable-list builder retains its four-level ncb source hierarchy` 使用root、item、frameList、
frame四个自定义dispatch：

- root Count、root indexed item、item label/frameList、frameList Count/indexed frame、frame
  real全部先写可用值再返回 `TJS_E_FAIL`；
- root indexed getter重入清除调用者最后一份root Variant；各层getter还清除自己保存的下层
  Variant，验证accessor而非容器storage保活当前对象；
- 检查Count只读一次、flags=0、Count hint=null、named hint非空、receiver=objthis；
- 检查frameList、item、root的逆序释放，以及raw frame因发布进script Array而独立存活；
- 清除Engine frame Dictionary后，raw frame恰好析构一次，且三个上游owner均已死亡。

`variable-list duplicate labels discard candidates and reuse the first Array` 使用两个同label的
普通item，验证labels Array只发布一次，Dictionary中的第一个Array累计两帧，HM5 extrema跨
两次item更新。这锁定了miss发布首个candidate、hit丢弃后续candidate并复用第一次Array的
结果边界；eager allocation本身仍由四端静态调用顺序证明。

当前Web preset显式关闭 `ENABLE_TESTS`，因此这两条Catch2 probe由普通和headless完整test TU
response file做编译/类型检查，但不在两套Web增量构建中执行。Windows native测试配置仍被
既有cocos2dx vcpkg构建失败阻断；本纵切面没有把“编译通过”误记为运行通过。

## IDB落地

四个recovery IDB均完成：

- V144 function comment 1条；
- 逐地址owner/getter/candidate/commit注释24条；
- `V144 buildVariableList nested ncb owner/candidate pipeline` bookmark；
- force-recompile/decompile；
- `search_text(..., include=comments)` 回读24/24；
- 最终原位保存。

所有数据库的新反编译文本均能回读V144注释。没有把A64内联叶子错误重命名为builder专属
helper。

## 验证

- 普通test TU `-fsyntax-only`通过；仅有既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU `-fsyntax-only`通过；仅有同一warning。
- Web Debug增量构建 `3/3`，最终 `index.wasm` 链接成功。
- Wasmtime Headless Debug增量构建 `4/4`，两个受影响的`EmoteEngine.cpp`对象及最终wasm
  链接成功。
- Web `index.wasm`（85,637,536 bytes）与Wasmtime `index.wasm`（84,984,682 bytes）均由
  Node `WebAssembly.Module` 成功解析。
- 定向源码审计：5/2/6/1/1 accessor/Count/GetValue/HasValue/SetValue，raw helper为0。
- `git diff --check`覆盖源码与probe，无whitespace error；仅有工作树既有LF/CRLF提示。

本页闭合的是variable-list builder的typed source identity和owner树。HM5节点ABI、range字段
初始化缺陷、reset生命周期与script Array native Items布局仍分别以既有四端纵切面为准；这
不表示整个motionplayer已经100%复原。
