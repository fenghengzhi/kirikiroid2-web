# MotionPlayer ResourceManager `findSource` / ObjSource 发布边界四参考恢复（2026-08-15）

## 范围与函数映射

本纵切面重新裁决 `ResourceManager::findSource` 的 path 分流、`blank` 字典、`src` raw-node
导航和 `ObjSource`→NCB adaptor 发布生命周期。证据仅来自 `reference/binaries/` 当前四份
参考二进制。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ResourceManager::findSource` | `0x6A7F1C` | `0x57BDE0` | `0x100102594` | `0xFF890` |
| path split | `0x6A7F84` | `0x57BE24` | `0x1001025EC` | `0xFF91A` |
| ObjSource allocation | `0x6A83A0` | `0x57BED4` | `0x100102710` | `0xFFA48` |
| adaptor→Variant wrapper | inline | `0x57C178` | `0x100102AB4` | `0xFFE54` |

主体继续使用已验证的剥离符号名 `ResourceManager_findSource_guess`；三份保留 out-of-line
wrapper 的 IDB 将其命名为 `ObjSource_toVariant_guess`。Android arm64 将同一发布序列内联。

## path 分流与边界

函数先用共享 `splitTtstrByDelimiter_guess` 按 `/` 拆分。该 helper 至少发布 final
remainder，因此 `pieces[0]` 总是存在；空 path 或 leading slash 使首元素为空并返回 Void。

首元素分流严格为：

```text
"blank" -> blank descriptor Dictionary
"src"   -> loaded module raw source lookup
other   -> Void
```

未知 prefix 在读取 `pieces[1]` 之前返回。`blank` 分支不读取 moduleKey，却不检查
`pieces[1]` 是否存在；随后按 `:` 再拆分并无条件读取 dims 0..3。`src` 分支同样不检查
outer pieces 长度，并且在查 module map 之前就读取、转换 group 与 icon 两个动态 key。
这些 malformed-path 情况是源级前置条件，不改写为保护性 miss。

## `blank` 字典数据流

`blank/<width>:<height>:<originX>:<originY>` 每次新建独立 Dictionary。四个维度都以拆分
得到的 `ttstr` 构造临时 String Variant，再按以下顺序用五个独立 member-hint global 写入：

```text
width, height, originX, originY : String
blank                           : Integer(1)
```

每个临时 Variant 都在对应 SetValue 返回后立即析构；SetValue 状态不形成额外分支。返回
Variant 以同一 Dictionary dispatch 同时填充 Object 与 ObjThis，随后释放 accessor 的临时
owner。即使 dictionary dispatch 为 null，机器码仍把结果 tag 写为 Object，而不是 Void。
正常 API 环境下创建失败通常在更早处抛出；当前源码的 `ncbDictionaryAccessor` 与
`tTJSVariant(dispatch, dispatch)` 保留该表示边界。

## `src` raw-node 导航

`src/<group>/<icon>` 先用原样、区分大小写的 moduleKey 查 loaded-module unordered_map；
miss 返回 Void，没有 fallback 全表扫描。命中后按以下顺序导航：

```text
root["source"]                  // fixed key: strict read
    [group]                     // dynamic key: Contains gate then strict read
    ["icon"]                   // fixed key: strict read
    [icon]                      // dynamic key: Contains gate then strict read
```

只有 group/icon 两个动态 key 有 Contains gate；`source`、`icon` 固定 key 缺失会遵循 strict
raw-node 异常边界。group raw node 是 full-expression 临时量，在 `icon` holder 构造后、
icon Contains gate 前已释放。最终 icon node 临时 owner 被复制进 ObjSource。

## ObjSource / adaptor 所有权

四端都分配一个 64 位 24 B、32 位 12 B 的 ObjSource：retained raw owner/node pair 加一个
置零的 lazy texture pointer。随后调用
`ncbInstanceAdaptor<ObjSource>::CreateAdaptor(src, sticky=false, err=false)`：

- class object 不存在或 `CreateNew` 失败：返回 null，`findSource` 写 Void；已构造 ObjSource
  没有 guard，也不 delete，因而泄漏 raw owner/node；
- script object 建立成功但取不到期望 native adaptor：CreateAdaptor 仍返回 script object，
  不抛错也不附加 src；`findSource` 返回这个 Object，而 src 仍泄漏；
- compatible adaptor：写入 src，sticky=false；script adaptor 失效/析构时会 delete ObjSource，
  ObjSource 先 Release lazy texture，再由 PSBRawNode 成员释放 raw owner；
- 成功发布 Variant 时，对同一 dispatch 分别建立 Object 与 ObjThis 引用，再 Release
  CreateNew 的初始 dispatch 引用。返回 Variant 最终持有两个引用。

三份 out-of-line wrapper 只有 `findSource` 一个 caller；Android arm64 内联了相同逻辑。
源码使用裸 `new ObjSource` 后直接 CreateAdaptor，正好保留 null、invalid-adaptor 与异常期的
未接管窗口，不应用 `unique_ptr` 修复。

## 源码与测试裁决

当前算法与四端一致，不需要语义改写。本轮从编译源码删除 `LABEL_11`、`v27` 等旧反编译
残片，补全 invalid-adaptor 的“返回 Object 但 native src 泄漏”边界。回归测试覆盖：

- `blank` 忽略 moduleKey、五字段类型/值与每次新鲜 Dictionary；
- empty/unknown prefix 的 early Void；
- 合法 `src/group/icon` 形状在 module miss 时 Void；
- 有效 module 下 dynamic group miss 时 Void。

未通过短 path 主动触发 `pieces[1]`/`dims[3]` 越界，也未伪造 NCB class/native-instance
故障；这些边界由四端控制流和真实 adaptor template 固定，不引入测试专用保护路径。

验证结果：

- 聚合 `motionplayer-dll.cpp` 复用 Web Debug 的 Emscripten defines/includes/ABI 参数执行
  `-fsyntax-only` 成功，唯一诊断为仓库既有 `_tss` deprecated warning；
- `cmake --build --preset "Web Debug Build"` 完成 ResourceManager 重编译、motionplayer
  静态库和最终 `index.html`/wasm 链接，只有仓库既有 Emscripten 警告；
- 四份 recovery IDB 已写入 path、blank、raw-node、pre-adaptor gap 与 CreateAdaptor 注释，
  添加 ObjSource 未接管窗口书签并全部保存；
- Android armv7、iOS arm64、iOS armv7 的单 caller wrapper 已统一命名为
  `ObjSource_toVariant_guess`，Android arm64 保留内联形态。
