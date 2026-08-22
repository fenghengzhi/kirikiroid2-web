# MotionPlayer ResourceManager `findMotion` dispatch 接管边界四参考复核（2026-08-15）

## 范围与地址

本纵切面只裁决 `ResourceManager::isExistMotion` 与 `findMotion` 的查询次序、返回结构及
异常期对象生命周期。证据来自 `reference/binaries/` 当前四份参考二进制；旧
`libkrkr2.so` 注释不参与裁决。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `isExistMotion` | `0x6A6AD8` | `0x57B780` | `0x100101AC8` | `0xFECF4` |
| `findMotion` | `0x6A72B4` | `0x57B9F8` | `0x100101E84` | `0xFF11C` |
| path split call | `0x6A6B3C` / `0x6A7324` | `0x57B7C2` / `0x57BA3E` | `0x100101B20` / `0x100101EE4` | `0xFED7C` / `0xFF1A6` |

四端 recovery IDB 中两个主体均保留 `_guess` 名称，因为参考产物已剥离原始 C++ 符号：
`ResourceManager_isExistMotion_guess`、`ResourceManager_findMotion_guess`。

## 查询数据流

两个主体共同执行以下流程：

1. 用 `/` 拆分 path，然后不检查长度，直接读取元素 1、2 作为 chara 与 motion；
2. project 为 Void 时跳过定向查找；非 Void 时必须已经是 String，其他 Variant 类型在
   string 转换处抛异常；
3. 精确 module key 命中后沿 `root["object"][chara]["motion"][motion]` 查询；
4. module 或 motion 定向 miss 后不会返回，而是从 loaded-module map 起点扫描全部记录，
   包括刚刚定向检查过的同一 module；
5. fallback 的首命中受各 ABI 的 unordered-map bucket/迭代拓扑支配，不承诺插入顺序或
   字典序。

`isExistMotion` 命中返回 true，彻底 miss 返回 false。`findMotion` 彻底 miss 返回 Void；
命中则每次新建 PSB raw-value dispatch 和两元素 TJS Array：

```text
[0] = 命中的 motion raw-node dispatch（Object 与 ObjThis 是同一指针）
[1] = 实际命中的 loaded-module map key
```

第二元素不是 project 参数。错误 project key 经 fallback 命中其他 module 时，返回的是
该 module 的真实 key。

## dispatch 所有权状态机

dispatch 大小在 64 位端为 48 B、32 位端为 24 B。四端普通路径完全同形：

```text
operator new + PSBValueDispatch ctor
    initial intrusive ref = 1，暂时只有裸指针
create Array
emplace [0] as Variant(dispatch, dispatch)
    Object AddRef + ObjThis AddRef
dispatch->Release()
    放弃 initial ref；Array 元素留下两个引用
emplace [1] matched key
return Array
```

因此真正的 ownership commit point 是首个 Array 元素成功构造，而不是 dispatch 构造或
Array 创建。直接命中和 fallback 命中都到达这段顺序；Android armv7 与两份 iOS 产物
保留两套机器码，Android arm64 则把两条查询分支汇入一个共同 handoff block。

## 异常与泄漏窗口

新鲜复核纠正了 2026-08-14 文档中过强的“任一点异常均回收 dispatch”结论：

- `operator new` 或 dispatch 构造失败：new-expression 的构造失败清理会 delete 分配；
- dispatch 构造已成功、首个 Array 元素尚未成功：Array 创建或首个 `emplace` 抛异常时，
  没有 landing pad/guard 对初始引用执行 `Release`，因此泄漏 dispatch 及其 raw owner/node
  引用；
- 首元素成功：Array 已拥有 Object 与 ObjThis 两个引用。显式 `Release` 放弃初始引用；
  第二元素复制或返回值传递再抛异常时，局部 Array 析构会释放两个引用；
- 正常返回：调用者持有这份新鲜 Array；函数不缓存 Array 或 dispatch。

iOS armv7 的 SjLj 路径最直接显示该边界：fallback 在 `0xFF438` 分配、`0xFF44A`
构造、`0xFF458` 创建 Array、`0xFF466` 首次 emplace、`0xFF474` 显式 Release，之后
`0xFF480` 才复制 matched key。清理 helper `0xFF514` 的 constructor-failure case 只做
`operator delete`；覆盖 Array 创建/首元素插入的后续 cases 析构局部 Array/raw-node
临时量，却没有 dispatch `Release`。直接命中分支在同一 helper 中具有平行 case。

其余三端交叉确认：

- Android armv7 fallback：`0x57BBDE` 分配、`0x57BBEA` 构造、`0x57BBF4` 创建 Array、
  `0x57BBFE` 首次 emplace、`0x57BC0A` Release、`0x57BC12` 复制 key；主体之后没有补偿
  landing block；
- iOS arm64 fallback：`0x100102194` 分配、`0x1001021A4` 构造、`0x1001021B0` 创建
  Array、`0x1001021C0` 首次 emplace、`0x1001021D0` Release、`0x1001021DC` 复制 key；
- Android arm64 的函数内 EH tail 只清理 Variant/raw-node 临时量及 constructor-failure
  allocation，没有在 commit point 前形成额外 dispatch virtual `Release`。

## 源码与测试裁决

当前 `ResourceManager.cpp` 原本已使用与参考一致的裸指针顺序：构造 dispatch、创建 Array、
首元素 emplace、显式 Release、再写 matched key。此次不引入 `unique_ptr` 或 scope guard，
因为那会修掉原版泄漏窗口；只把旧绝对地址注释迁移到本文并在源码注明 ownership commit
边界。

单元测试补齐以下稳定、非分配故障行为：direct hit、Void fallback、错误 project key 的
full-scan fallback、实际 matched key、每次新鲜 Array、彻底 miss 的 false/Void，以及
非 String project 的转换异常。分配失败泄漏不通过注入 allocator 故障模拟，以免伪造
参考二进制没有提供的测试钩子。

验证结果：

- 聚合 `motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten defines/includes/ABI 参数
  执行 `-fsyntax-only` 成功，唯一诊断为仓库既有 `_tss` deprecated warning；
- `cmake --build --preset "Web Debug Build"` 完成 ResourceManager 重编译、motionplayer
  静态库及最终 `index.html`/wasm 链接，只有仓库既有 Emscripten 警告；
- 四份 recovery IDB 均已写入 handoff/commit/cleanup 注释与 pre-commit 泄漏窗口书签并
  保存；iOS armv7 SjLj helper 命名为
  `ResourceManager_findMotion_sjlj_cleanup_guess`；
- 本纵切相关 tracked 文件通过 `git diff --check`，新增文档与修改文件均无行尾空白。
