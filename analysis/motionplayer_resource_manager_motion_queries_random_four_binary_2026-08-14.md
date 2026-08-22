# MotionPlayer ResourceManager motion 查询与 random 四参考二进制恢复（2026-08-14）

## 范围

本纵切面复核 `ResourceManager` registrar 中紧邻 source/layer-id 主体的三个成员：
`isExistMotion`、`findMotion`、`random`。所有语义裁决只使用当前
`reference/binaries/` 四份二进制；旧 `libkrkr2.so` 记录仅作待证线索。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `isExistMotion` | `0x6A6AD8` | `0x57B780` | `0x100101AC8` | `0xFECF4` |
| `findMotion` | `0x6A72B4` | `0x57B9F8` | `0x100101E84` | `0xFF11C` |
| `random` | `0x6A894C` | `0x57C1CC` | `0x100102C90` | `0x1000F0` |
| `Player::random` | `0x6B7B98` | `0x585100` | `0x10010DE8C` | `0x10B774` |

四份 recovery IDB 已统一命名为 `ResourceManager_{isExistMotion,findMotion,random}_guess`。

## motion path 与查找顺序

两个查询都先按 `/` 拆分 path，然后直接读取结果元素 1 与 2 作为 chara 和 motion 名；
主体没有先检查元素数。因此少于三个分段不是“安全返回 miss”的 API 边界，源级
`pieces[1]/pieces[2]` 的越界前置条件必须保留。

共同查找流程：

1. project Variant 为 Void 时跳过定向查找；非 Void 时必须已经是 String，其他类型按
   TJS Variant-to-string 类型错误抛异常；随后以精确、区分大小写的 `ttstr` key 查
   loaded-module map；
2. 定向 module 命中后，按 raw PSB 链查
   `root["object"][chara]["motion"][motionName]`；
3. 定向查找未命中 module 或 raw motion 后，仍从 loaded-module map 的全局迭代链起点
   扫描全部记录；没有“project 非 Void 就限制在该项目”的 early return；
4. 第一条命中立即返回。unordered-map 的遍历顺序来自各平台 STL 容器，不是插入顺序
   或字典序；多个 module 含相同 chara/motion 时，Void/miss fallback 的 matched key
   可随 ABI/bucket 拓扑变化。

`isExistMotion` 只返回 bool。`findMotion` 在命中时构造新的 raw PSB value dispatch，
再建立独立的两元素 TJS Array：

```text
[0] = motion raw-node dispatch
[1] = 实际命中的 loaded-module map key
```

元素 1 不是原始 project 参数：即使非 Void project 的定向查找失败后由另一 module
回退命中，返回的仍是另一 module 的 key。无命中返回 Void，不返回空 Array。

## 对象生命周期与异常边界

四端的 dispatch 大小随指针宽度为 48/24 B。构造时 raw owner 与 node 被保留；首个 Array
元素用同一 dispatch 填入 Object 与 ObjThis，因而建立两个 intrusive 引用。只有该元素
构造成功后，主体才 Release 构造产生的初始引用。第二元素复制 map node 内的 key string
handle。

2026-08-15 对四端异常落点重新逐条复核后，旧版“任一点抛异常都会释放 dispatch”的描述
被证伪。`new`/dispatch 构造自身失败时，new-expression 会释放分配；但 dispatch 构造成功
后到首个 Array 元素成功接管前，原版没有独立 owner。因此 Array 创建或首个 `emplace`
失败会泄漏这个 dispatch。首元素成功后，Array 已拥有 Object/ObjThis 两个引用，随后的
matched-key 复制或返回值转移失败才会由 Array 析构完整回收。当前源码保留 raw pointer
与“首元素成功后才 Release”的形状，不应用 RAII guard 消除这个可观察边界。函数不缓存
返回 dispatch，每次相同查询均得到新的外层 dispatch/Array owner。

## random 的真实成员形状

四个 registrar 都把 `random` 绑定为普通的无参数原生成员，主体返回 `double`。它不是
raw NCB callback，也不通过 `objthis` 再查询 native instance。共同流程为：

```cpp
double ResourceManager::random() {
    Variant result;                       // Void-initialized
    randomGenerator.FuncCall("random", &result, 0, nullptr);
    return result.AsReal();
}
```

- generator 就是构造期保存的 `new Math.RandomGenerator()` Variant；
- Variant 必须是 Object；否则对象转换按 TJS 规则抛异常；
- 调用 `random` 时 `numparams=0`、`params=nullptr`、`objthis=generator`；
- native 主体不检查 `FuncCall` 返回码；无论状态如何都对结果槽执行 `AsReal()`；
- 本仓库 `AsReal(Void)` 为 `0.0`，Object/Octet 等不可转换值抛普通 Variant conversion
  exception；结果 Variant 随后析构；
- NCB 的普通 typed invoker 再把返回 double 写入脚本 result 槽。额外脚本参数由 typed
  invoker 的普通零参成员协议处理，native 主体本身没有 raw callback 参数计数逻辑。

## 源码修复与验证

当前 Web 先前把 `random` 注册成 raw callback，并在 `FuncCall` 失败时跳过结果转换。现已：

- 将声明/定义恢复为 `double ResourceManager::random()`；
- 保留零参 generator `FuncCall`，明确忽略状态并无条件 `result.AsReal()`；
- registrar 从 `NCB_METHOD_RAW_CALLBACK` 改为普通 `NCB_METHOD(random)`。

## Player 上一层 random dispatch

四端的 `Player::random` 不是当前源先前实现的保护式 wrapper。ResourceManager Variant 的
成员偏移分别为 Android arm64 `+0x3E0`、Android armv7 `+0x2AC`、iOS arm64
`+0x370`、iOS armv7 `+0x26C`，共同数据流为：

1. 把 Player 的 canonical ResourceManager Variant 复制到栈上，先建立一个独立 Variant
   owner；
2. 对这个副本执行带 AddRef 的 Object 转换，取得独立 dispatch owner，然后立即析构
   Variant 副本；
3. 构造 Void result，以 retained dispatch 同时作为调用对象和 `objthis`，零参数调用
   `random`；
4. 不检查 `FuncCall` 状态，无条件对 result 执行 `AsReal()`；
5. 正常路径先析构 result，再 Release dispatch。异常表对已构造的 result、dispatch 和
   早期 Variant 副本执行对应的逆序清理。

因此这里有两个可观察边界：

- `FuncCall` 返回失败但已经写入可转换数值时，Player 仍返回该数值；
- ResourceManager 成员不是 Object 时，Object 转换抛异常，不返回保护性的 `0.0`。

Object Variant 内部 dispatch 为空时，参考代码在取得空指针后仍进入虚调用；它没有
源级 null guard。当前实现同样不把这个无效内部状态悄悄改写成 `0.0`。

2026-08-16 V165 fresh 四库复核进一步证明，这个 Player 调用点和下层
ResourceManager->RandomGenerator 调用点不是各自拥有 local static hint，而是共享同一
32-bit process-wide backing word；ResourceManager 侧直接借用持久 generator，单次调用
没有 Variant copy 或 AddRef/Release。Android 两端还各保留一份把 Player wrapper 完整
内联的零引用 range-helper 副本。地址、owner 计数、A64 recovery-code 修复和回归探针详见
`motionplayer_shared_random_hint_owner_lifecycle_four_binary_2026-08-16.md`。

源码现按该顺序复制 Variant、用 `AsObject()` 保留 dispatch、提前 `Clear()` 副本，并借
已有释放守卫覆盖正常和异常退出；调用状态明确丢弃，返回 `result.AsReal()`。新增回归探针
使 `random` 返回 `TJS_E_FAIL` 同时写入 `0.625`，验证 Player 仍返回 `0.625`；另以整数
ResourceManager 验证对象转换异常。

`isExistMotion`/`findMotion` 当前源级 direct-then-full-scan、实际 matched key 和新鲜
Array/dispatch 行为与四端一致。2026-08-15 的补充复核只迁移了旧地址注释，并明确记录
上述原版 dispatch 接管前泄漏窗口；没有用保护性 owner 改写它。

验证：

- `Web Debug Build` 全 preset 编译/链接完成，续跑为 `ninja: no work to do.`；
- 完整 `motionplayer-dll.cpp` 复用 Web Debug 的真实 Emscripten defines/includes/ABI 参数
  做 `-fsyntax-only` 成功，唯一诊断为仓库既有 `_tss` deprecated warning；
- 四份 recovery IDB 已补齐函数名、random/isExistMotion 原型、findMotion sret/usercall
  形状、`Player_random_guess` 的 double 原型和函数级生命周期注释，并保存到四份
  recovery IDB；
- Player 修复后的 `Web Debug Build` 完整编译/链接成功；聚合测试翻译单元也复用 Web
  Debug 的真实 Emscripten defines/includes/ABI 参数通过 `-fsyntax-only`，唯一诊断仍为
  仓库既有 `_tss` deprecated warning。

## 未外推的部分

- unordered-map fallback 的具体首命中 key 是容器运行时状态，不把某个 fixture 的结果
  硬编码成跨 ABI 顺序保证；
- 本纵切面没有把各编译器内联的 raw-node临时 holder 与 Array growth helper 全部命名；
- 本纵切面只闭合 ResourceManager 与 Player 两级 `random` 调用；更上层粒子/插值调用者
  的取样次数、算术顺序与随机数消费顺序仍按各自纵切面裁决，不能从本成员反向外推。
