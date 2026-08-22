# Player / ResourceManager `random` 共享 hint 与接收者生命周期四参考恢复（2026-08-16）

## 1. 结论

四个当前参考二进制都把两层随机调用绑定到**同一个** 32-bit TJS member-hint backing
word：

```text
Player::random()
  -> ResourceManager dispatch 的 "random"
     -> ResourceManager::random()
        -> 持久 Math.RandomGenerator dispatch 的 "random"
```

这不是两个互不相关的函数内 `static tjs_uint32 hint`。Player 的调用点、
ResourceManager 的调用点以及 Android 中把 Player 包装器内联进去的 range-helper 副本，
都把同一绝对地址作为 `FuncCall` 的第四参数。旧 Web 源码在两个函数里各声明一个 local
static，因此缓存身份与原版不一致；本轮改为一个 `detail::randomMemberHint_guess` 进程期槽。

两层接收者所有权刻意不同：

- Player 先复制 canonical ResourceManager Object/ObjThis closure，再用 `AsObject()` 额外
  AddRef 一个 raw dispatch owner，随即销毁 closure 副本；回调期间由这一个 raw owner
  保证 receiver 存活，结果转换后才 Release；
- ResourceManager 直接在持久 `_randomGenerator` Variant 上严格取 Object，并借用该
  dispatch；单次调用不复制 Variant，也不 AddRef/Release generator。

两层均忽略普通 `FuncCall` HRESULT，并无条件对默认 Void 的 result Variant 执行
`AsReal()`。所以“普通失败但写入数值”仍返回数值，“普通失败且未写 result”返回 `+0.0`；
Object/Octet 等不可转换 result 仍抛 TJS conversion exception。两层都没有保护性状态判断、
null fallback 或伪造随机值。

## 2. UTF-16 literal 与函数定位

fresh raw-byte 搜索使用：

```text
72 00 61 00 6E 00 64 00 6F 00 6D 00 00 00
```

Android 两库各只有一个 hit。iOS 两库各有四个 hit，其中后三个属于其他 runtime/library
代码；motionplayer 生产链只引用表中第一个。IDA 对 iOS 字符串曾把开头显示为窄字符
`"r"`、后半段另建 `aAndom`；原始 byte pattern 与完整调用点共同证明它仍是同一
UTF-16LE `random` literal。

| 目标 | production literal | `ResourceManager::random` | size | `Player::random` | size |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x14CC24C` | `0x6A894C` | `0x128` | `0x6B7B98` | `0x198` |
| Android armv7 | `0xD7E5A0` | `0x57C1CC` | `0x6C` | `0x585100` | `0x82` |
| iOS arm64 | `0x10195BE96` | `0x100102C90` | `0x9C` | `0x10010DE8C` | `0xA8` |
| iOS armv7 | `0x174E1FA` | `0x1000F0` | `0xC6` | `0x10B774` | `0xDA` |

四库把 literal 起点统一命名为 `str_random_utf16_guess`。函数名继续保留 `_guess`，因为
四份发行文件都已 stripped；语义由字段偏移、调用形状和完整 consumer 链证明，不能据此
虚构原始 C++ 拼写。

## 3. 共享 member-hint storage

| 目标 | shared hint | 直接消费函数 |
|---|---:|---|
| Android arm64 | `0x1AB5300` | RM random、out-of-line Player random、inlined range 副本 |
| Android armv7 | `0x11117F8` | RM random、out-of-line Player random、inlined range 副本 |
| iOS arm64 | `0x101B697C8` | RM random、out-of-line Player random |
| iOS armv7 | `0x187D4C8` | RM random、out-of-line Player random |

AArch64 的 ADRP/ADD 与 Thumb literal materialization 可能为同一逻辑引用产生两条 IDA
data xref；按 owning function 去重才是上表的 consumer 数量。Android 的短 range helper
只调用 out-of-line Player random，不直接加载 hint，所以不另算直接 consumer。

Android arm64 的 recovery IDB 原先只把 `Player_random_guess` 的第一条 `SUB SP` 定义为
code，余下 `0x194` bytes 虽属于函数范围，却没有 instruction entities；Hex-Rays 因而只
显示 `JUMPOUT`，hint xref 审计也漏掉该函数。本轮 fresh old-address 复核恢复了完整 101 条
AArch64 指令、五分支 Variant-to-Real jump table、shared-hint operand 与 EH tail。恢复后的
`0x6B7C38` 虚调用明确接收 `&g_randomMemberHint_guess`，并重新闭合 17 条 caller xref：
短 range helper 一条、particle update 十六条。

四库均将 backing word 重建为独立 size-4 global entity，命名
`g_randomMemberHint_guess`。这只表达已证实的存储身份；它不声称已经恢复原始变量名、原始
translation unit 或与附近其他静态对象的源级声明次序。

## 4. Player 层的数据流与所有权

canonical ResourceManager Variant 的四端偏移为：

| 目标 | Player canonical RM offset | Player call site |
|---|---:|---:|
| Android arm64 | `+0x3E0` | `0x6B7C38` |
| Android armv7 | `+0x2AC` | `0x585148` |
| iOS arm64 | `+0x370` | `0x10010DEF4` |
| iOS armv7 | `+0x26C` | `0x10B80A` |

共同伪代码为：

```cpp
double Player::random() {
    tTJSVariant rmCopy(_resourceManager); // CopyRef Object + ObjThis
    iTJSDispatch2 *rm = rmCopy.AsObject(); // 独立 AddRef
    rmCopy.Clear();                        // 先释放 closure 的两个引用

    tTJSVariant result;                    // Void
    (void)rm->FuncCall(
        0, TJS_W("random"), &randomMemberHint_guess,
        &result, 0, nullptr, rm);
    double value = result.AsReal();
    // normal cleanup: result dtor -> rm Release
    return value;
}
```

因此在 callback 入口，相对调用前恰好发生三次 AddRef、两次 Release，净保留一个 raw
receiver owner。正常返回时再 Release 该 owner，单次调用的 AddRef/Release 总数重新相等。
这不是“直接借用 canonical Variant”也不是“让局部 Variant 活到 callback 之后”。回调若
重入清空 canonical owner 和外部 owner，已捕获的 raw receiver 仍固定且存活；异常表也会
按已构造阶段释放 result 与 raw receiver。

非 Object canonical Variant 在 `AsObject()` 严格转换处抛出。内部 Object dispatch 本身为
null 时，原版随后仍走虚调用，没有 null guard；源码没有用 `0.0` fallback 掩盖该无效状态。

## 5. ResourceManager 层的数据流与所有权

持久 RandomGenerator Variant 的四端偏移与调用点为：

| 目标 | `_randomGenerator` offset | type word | generator call site |
|---|---:|---:|---:|
| Android arm64 | `+0x90` | `+0xA0` | `0x6A89C0` |
| Android armv7 | `+0x50` | `+0x58` | `0x57C20C` |
| iOS arm64 | `+0x88` | `+0x98` | `0x100102CFC` |
| iOS armv7 | `+0x4C` | `+0x54` | `0x100180` |

共同伪代码为：

```cpp
double ResourceManager::random() {
    iTJSDispatch2 *generator = _randomGenerator.AsObjectNoAddRef();
    tTJSVariant result;
    (void)generator->FuncCall(
        0, TJS_W("random"), &randomMemberHint_guess,
        &result, 0, nullptr, generator);
    return result.AsReal();
}
```

四端都在持久 Variant 本体上先读 type/执行严格 Object conversion，再读 Object dispatch。
没有临时 Variant copy、没有 closure CopyRef、没有 per-call generator AddRef，也没有正常尾部
generator Release；receiver 与 objthis 都是同一借用 dispatch。持久 owner 的生命周期由
ResourceManager 构造期 `new Math.RandomGenerator()` 写入和 ResourceManager 析构负责。

## 6. 调用参数、状态与转换边界

两层调用的 ABI 形状完全相同：

- `flags = 0`；
- `membername = L"random"`；
- hint 指向上表的同一 32-bit backing word；
- result 非空且进入调用前为 Void；
- `numparams = 0`；
- `params = nullptr`；
- `objthis == receiver`。

ordinary HRESULT 在调用后不参与任何 branch。随后 result 的 TJS Real conversion profile
为：Void -> `+0.0`，Integer -> double，Real 原样返回，String 按 TJS 数字解析；Object 和
Octet 抛 conversion error。若 `FuncCall` 自身或 `AsReal` 抛异常，已构造 owner 按对应层的
EH 表清理；普通负 HRESULT 本身不会抛，也不会触发清零。

## 7. Android range-helper 双副本

fresh 四库复核证明两个 Android 链接产物都保留两份零 caller、零 registrar entry 的相同
区间语义，而不是只有一份：

| 目标 | short copy（调用 Player random） | size | inlined-random copy | size |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6B7B60` | `0x38` | `0x6B3240` | `0x1BC` |
| Android armv7 | `0x5850C8` | `0x38` | `0x582400` | `0xAC` |
| iOS arm64 | 被裁剪 | — | 被裁剪 | — |
| iOS armv7 | 被裁剪 | — | 被裁剪 | — |

两份 body 都实现：

```cpp
double range(Player *self, double minimum, double maximum) {
    double value = minimum;
    if(minimum != maximum)
        value = minimum + (maximum - minimum) * self->random();
    return value;
}
```

短副本调用邻接的 out-of-line `Player_random_guess`；另一副本把第 4 节整条 owner/callback/
conversion 流程内联。两者自身均无 caller，iOS 链接产物均裁掉 range helper，但保留供粒子
系统使用的 out-of-line Player random。最合理的恢复仍是一个源级
`Player::randomInRange_guess`，不能为了模仿编译器/COMDAT/translation-unit 发射结果在源码
中伪造第二个 API。IDB 将第二份保守命名为
`Player_randomInRange_inlinedRandomCopy_guess`。

直接 `minimum != maximum` 的边界也保持不变：相等有限值、同号无穷和 `-0.0/+0.0` 不
消费 RNG，并返回 minimum 的原始位型；NaN 与自身不等，消费一次 RNG，算术结果仍为 NaN。

## 8. 源码与回归探针

本轮源码改动：

1. 在 `MotionDispatch.h` / `RuntimeSupport.cpp` 声明并定义一个
   `randomMemberHint_guess`；
2. Player 与 ResourceManager 两个 wrapper 都改用该槽，删除各自 local static；
3. 更新 Player header，明确 Android 两份 body 是链接/优化器产物，iOS 不保留 range
   helper；
4. 不改变 Player 的 retained-receiver guard，也不把 ResourceManager 的借用 generator
   改成 RAII owner。

回归源码新增/扩展三组探针：

- Player ordinary failure 写入 `0.625` 仍返回该值，并观察 callback 前 `+3 AddRef/+2
  Release`、返回后 `+3/+3`，两次调用拿到同一 shared hint；
- 独立 heap receiver 在 callback 内清空 Player canonical owner 与 external owner，确认
  捕获 dispatch 继续完成调用，最后由 Player 剩余构造期 owner/析构链释放；
- 测试脚本环境把 `Math.RandomGenerator` 临时替换为可观察 factory，确认
  ResourceManager 两次/三次 `random` 调用使用同一 shared hint，单次调用前后 generator
  AddRef/Release 计数不变，普通失败写数值仍转换、未写 result 得到 `+0.0`。

这些 probe 没有向生产类加入新的 generator setter，也没有放宽 `_randomGenerator` 的私有
所有权边界。

## 9. IDB 写回与验证

四份 recovery IDB 均已完成：

- production literal 语义命名；
- shared hint 独立 size-4 entity、命名、consumer 注释；
- ResourceManager/Player call-shape 与 owner 注释；
- Android 两份 range body 的差异命名；
- Android arm64 缺失 code entities、jump table 与 hint operand 恢复；
- `V165 complete shared random hint and owner boundaries` bookmark；
- affected functions force-recompile/readback，随后四库原位保存；A64 修复后再次保存。

工程验证：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整测试 TU syntax-only 均通过，只有仓库
  既有 `_tss` deprecated warning；
- Web Debug 与 Wasmtime/Headless Debug 最终链接完成；首次 Headless 重链曾因先前超时
  链接仍持有 `index.wasm` 而在 `llvm-objcopy` 报 permission denied，残留进程自然完成后
  续跑为 `ninja: no work to do.`，不是源码/链接错误；
- Web wasm：`85,648,335` bytes，539 imports / 69 exports；
- Headless wasm：`84,995,476` bytes，538 imports / 69 exports；
- 两个 wasm 均由 Node `WebAssembly.Module` 成功解析，并由 `llvm-objdump -h` 完整列出
  TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM/DATACOUNT/CODE/DATA/name/
  target_features sections；
- 两配置 CTest 均未注册测试；定向 `git diff --check` 无 whitespace error，仅现有
  LF/CRLF 提示。

相对 V164，两份 wasm 都精确增加 41 bytes，import/export 数不变；这与新增一个共享
4-byte mutable storage 及相应符号/调试元数据一致，不代表脚本接口面发生变化。

## 10. 未外推部分

- 本纵切面恢复的是两层 wrapper、共享 hint、owner 和转换边界，不声称恢复
  `Math.RandomGenerator` 内部 PRNG 算法；
- hint 的原始变量名、源文件归属和物理声明顺序仍未知，继续用 `_guess`；
- Android 双 body 被视为优化/链接产物，不据此推断两个不同的原始 public member；
- 粒子系统 16 个静态调用点、条件消费与单粒子取样顺序继续由既有 particle/random
  consumer 纵切面裁决，本轮只修订其中过时的“双副本”描述。
