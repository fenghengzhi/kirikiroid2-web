# `Player::resolveRenderSource` fast-path accessor chain（四参考，2026-08-16）

## 结论

四端 `Player_resolveRenderSource_guess` 的 internal-Layer fast path 都构造三个有序
`ncbPropAccessor` owner：descriptor、color、work。三个 accessor 分别负责三种不能混淆的读取：

1. descriptor 的 `blendMode` 是一次带 process-wide hint 的
   `GetValue<tjs_int>(name, Tag, flags=0, hint)`；
2. color 的索引 `0..3` 各执行一次
   `GetValue<tjs_int>(index, Tag, flags=0)`，没有 `HasValue` probe；
3. work 在 `assignImages(primary)` 后，按 `height -> width` 分别执行带 hint 的
   `HasValue(name,hint)`；任意非负 probe status 才进入第二次 flags=0
   `GetValue<tjs_int>`，negative probe 返回默认 0。

旧本地实现保留了这些可观察调用次数和返回码边界，但通过 `GetDispatch()`、手写 `PropGet` 和
独立裸 dispatch helper 隐去了真实模板调用。现已直接使用三个已存在的 accessor；没有增加
owner、probe 或保护分支。

## 四端 fast-path 映射

| 目标 | resolver | descriptor vptr / blend get | color vptr / indexed get | work vptr / assignImages |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6BEF50` | `0x6BF0CC` / `0x6BF12C` | 优化后无可见 vptr / `0x6BF19C,0x6BF234,0x6BF2CC,0x6BF364` | `0x6BF3F4` / `0x6BF47C` |
| Android armv7 | `0x58AD94` | `0x58AE5A` / `0x58AE82` | `0x58AE94` / `0x58AEBE` loop | `0x58AEDC` / `0x58AF14` |
| iOS arm64 | `0x1001143E0` | `0x10011452C` / `0x100114564` | `0x100114578` / `0x1001145B0` loop | `0x1001145D4` / `0x100114630` |
| iOS armv7 | `0x111E08` | `0x111F70` / `0x111FA8` | `0x111FBC` / `0x111FEE` loop | `0x11200C` / `0x11205C` |

Android arm64 完全展开四次 color read，并把 color accessor 的 vptr 初始化/析构静态消去；其
Variant copy、`AsObject()` owning AddRef、四次同一 dispatch 的 `PropGetByNum` 和尾部 Release
仍与另外三端同构。这是 optimizer 差异，不是 raw dispatch 源类型差异。

fast path 的入口条件在四端仍严格为：source Variant type 是 Object、primary internal Layer
Variant type 是 Object、且两个 Object dispatch pointer 相等。没有额外 non-null gate；两个
typed-null Object 仍可相等。该条件和 fallback `SourceCache.loadSource(source, descriptor)` 在本轮
不变。

## Work dimensions 与 cleanup

| 目标 | height probe / get | width probe / get | cleanup work -> color -> descriptor |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6BF4BC` / `0x6BF4EC` | `0x6BF520` / `0x6BF550` | `0x6BF588` / `0x6BF598` / `0x6BF5B4` |
| Android armv7 | `0x58AF30` / `0x58AF4C` | `0x58AF5A` / `0x58AF76` | `0x58AFA0` / `0x58AFB6` / `0x58AFCE` |
| iOS arm64 | `0x100114658` / `0x100114680` | `0x10011469C` / `0x1001146C4` | `0x1001146F8` / `0x100114710` / `0x10011472C` |
| iOS armv7 | `0x11208A` / `0x1120B6` | `0x1120D4` / `0x112100` | `0x11212A` / `0x11213C` / `0x112152` |

descriptor accessor 跨过 blendMode、color/work 的整个生命周期；color 跨过四次 indexed read、
work `assignImages`、尺寸读取和 tint；work 从 persistent work Variant 构造后先执行
`assignImages(primary)`，其返回 Variant 是 resolver 的返回值，然后才读取自身尺寸并执行
corner tint。正常退出按 work、color、descriptor 逆序释放。不能把三者缩成一个复用 accessor，
也不能在各自最后一次属性调用后提前 Release，因为脚本 getter/`assignImages` 可以重入。

## 单读与双读边界

`blendMode` 和 colors 使用 `GetValue` 而不是 `getIntValue`。模板共同执行一次 getter、忽略
getter status、转换该次临时 Variant并销毁：

```text
blendMode: PropGet(0,"blendMode",sharedHint) -> AsInteger
colors[i]: PropGetByNum(0,i) -> AsInteger, i=0..3
```

所以失败状态若仍写入 Variant，写入值必须被消费；失败且不写入时，默认 Void 转 integer 得
0。添加 `HasValue` 会把脚本 getter 调用数翻倍并允许 probe 重入改变第二次结果，因此不是等价
的“安全改进”。

work dimensions 则故意是双读：probe Variant 总会销毁且不参与转换，任意非负 status 都进入
ordinary get；第二次 status 同样忽略。该边界与
`motionplayer_internal_workspace_dimension_ncb_accessor_four_binary_2026-08-16.md` 的
materializer/accurate-SLA 尺寸读取完全一致，但 owner 是 resolver 的 work accessor。

## 本地恢复与 IDB

- `SourceCache.cpp`：
  - 删除 fast path 专用的裸 `getRenderSourcePropertyInt_guess`；
  - descriptor 直接调用 hinted named `GetValue<tjs_int>`；
  - color 直接调用四次 indexed `GetValue<tjs_int>`；
  - work dimension helper 改为借用 `ncbPropAccessor&` 并调用 hinted `HasValue`/`GetValue`；
- 单元探针以 getter 返回 `TJS_E_FAIL` 但写值，锁定 named/indexed `GetValue` 都只调用一次、
  flags=0、原 accessor dispatch 同时作为 receiver/objthis、hint 只用于 named read、索引顺序
  `0,1,2,3`；
- Android armv7、iOS arm64、iOS armv7 的 numeric template 实例分别命名为
  `ncbPropAccessor_GetValueArrayInteger_guess` 并补 prototype；Android arm64 对应代码已内联；
- 四份 resolver 和 template/block 已补 comment/bookmark，force decompile readback 后全部原位保存。

## 验证

- ordinary motionplayer syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` motionplayer syntax-only：通过；
- Web Debug 完整增量构建/最终链接：`3/3`，通过；
- Wasmtime Headless Debug 完整增量构建：`4/4`，通过；
- syntax-only 与 C++ 编译只报告仓库既有 `_tss` literal-operator warning；Web 链接只报告既有
  pthread memory-growth、JSPI 与 JS-library warning；
- scoped source scan 与 `git diff --check`：通过。
