# EmoteEngine → Player new-expression、owner 发布与异常回滚四参考闭环（V261）

日期：2026-08-18（2026-08-20 最终校验）

## 1. 结论

四个参考二进制共同证明，`EmoteEngine` 的第一个 direct owner 来自一个普通 C++ member
initializer：

```cpp
_player(new Player(rmDispatch))
```

这个表达式在机器码中严格分成三个时刻：

1. `operator new(sizeof(Player))` 返回尚未被 Engine 拥有的 raw allocation；
2. 在 raw allocation 上执行完整 `Player` constructor；
3. constructor 正常返回后，才把 pointer 写入 Engine 的 `_player` owner slot。

因此有两条不能合并的失败路径：

- `Player` constructor 抛出：new-expression 只对 pending raw allocation 调用
  `operator delete`。`Player` 自己已经按 V260 完成 partial-member unwind，但完整
  `Player::~Player()` 不会运行；Engine owner slot尚未发布，第一个 controller也尚未开始。
- owner 已发布后的任一失败（最早可以是 position controller allocation/constructor）：
  Engine 把 `_player` 当作 completed member逆序析构，执行完整 `Player` destructor，再调用
  `operator delete`。

这不是“先把 slot 置 null、再手工 new”的容错实现，也不是 constructor body内的迟延
`reset()`。尤其 Android arm64中，`Engine+0x428` 在 Player new-expression前并没有独立清零；
失败路径之所以安全，是因为未完成的 owner member根本不进入 Engine prefix unwind，而不是因为
slot里预先存在 null。

## 2. 四端定位与发布边界

所有地址只在表中所属参考二进制内有效。

| 参考二进制 | Engine constructor | Player allocation size / `operator new` | Player ctor call | owner publish | `_player` offset | first position-controller allocation |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x67B76C` | `0x568` / `0x67BA18` | `0x67BA28` | `0x67BA2C` | `+0x428` | `0x67BA30`, size `0x80` |
| Android armabi-v7a | `0x560948` | `0x3B0` / `0x560AC0` | `0x560ACA` | `0x560ACE` | `+0x214` | `0x560AD2`, size `0x48` |
| iOS arm64 | `0x1001B7FB0` | `0x4B8` / `0x1001B8038` | `0x1001B8048` | `0x1001B804C` | `+0x2B8` | `0x1001B8050`, size `0x60` |
| iOS armv7 | `0x1B7788` | `0x348` / `0x1B784C` | `0x1B785A` | `0x1B7862` | `+0x15C` | publication后紧接 allocation, size `0x38` |

四端的 Player allocation size分别精确等于 V258闭合的本端 `sizeof(Player)`；紧随发布点的
allocation则精确等于同端 `EmoteVarController(2)` object size。这同时确认：

- 被发布的并非 adaptor、base subobject或 factory wrapper；
- `_player` 是一个 pointer-sized direct owner；
- position controller是 owner publication之后的第一个可能失败 frontier。

## 3. 共同源码级控制流

把 ABI-specific EH降回 C++ 语义后，四端共同控制流为：

```cpp
void *pending = operator new(sizeof(Player));
try {
    Player *complete = new (pending) Player(rmDispatch);
    // 只有上行正常返回，unique owner member才完成构造并发布 pointer。
    this->_player = unique_owner<Player>(complete);
} catch (...) {
    operator delete(pending);
    // Player partial members由Player constructor自己的EH ladder处理；
    // 不调用完整Player destructor，不读取/清空Engine owner slot。
    throw;
}

// 下一 member initializer；从这里开始失败会析构completed _player。
this->_ctlPosition = unique_owner<EmoteVarController>(
    new EmoteVarController(2));
```

以上伪码只用于展示编译器展开；portable源码不应手写它。普通 C++ new-expression与
`unique_ptr` member initializer会自然生成这套 pending-allocation cleanup和completed-prefix
unwind。

## 4. Android arm64：raw-delete landing与published-owner cleanup

### 4.1 正常路径

- `0x67BA14`：`MOV W0, #0x568`；
- `0x67BA18`：`operator new`；
- `0x67BA1C`：raw pointer保存到 `X22`；
- `0x67BA28`：以 `X22` 为 this调用 `Player` constructor；
- `0x67BA2C`：只有返回后才 `STR X22, [X19,#0x428]`；
- `0x67BA30`：开始分配第一个 position controller。

### 4.2 Player constructor抛出

landing entry `0x67BFD0`：

```text
preserve current exception
X0 = X22
operator delete(X0)
jump to the earlier Engine-prefix cleanup
```

这条边不读取 `Engine+0x428`、不调用 `Player` destructor，也不经过任何 controller owner。

### 4.3 publication后的失败

completed-prefix cleanup在 `0x67C268`读取 `Engine+0x428`，随后：

- `0x67C270`：调用完整 `Player` destructor；
- `0x67C27C`：调用 `operator delete`；
- slot在该 completed-owner路径上被清理。

raw-delete landing和completed-owner cleanup是两个独立 entry；把它们合成一个“若非空则
delete Player”会错误调用尚未完成对象的析构函数。

## 5. Android armv7：相同三阶段的 Itanium EH lowering

Android armv7的正常指令流同样没有把 raw `R5`提前写入 owner：

- `0x560ABC` 装入 `0x3B0`；
- `0x560AC0` 调用 `operator new`；
- `0x560AC4` 保留 raw result到 `R5`；
- `0x560ACA` 调用 `Player` constructor；
- `0x560ACE` 才发布到 `Engine+0x214`；
- `0x560AD2` 开始 position controller allocation。

本端的 Itanium EH cleanup被编译器拆入共享 landing fragments，IDA尚未把所有 fragment恢复为
一个便于独立命名的函数；但 call-site保护区精确覆盖 constructor-before-store窗口，raw
allocation仍在 `R5`，而 publication store位于正常返回边。与其余三端显式 cleanup和同一
Itanium new-expression ABI联合后，唯一一致的共享源码是 direct member new-expression：
constructor failure释放 pending `R5`并跳过 owner/controllers；publication后失败才进入
completed-owner prefix cleanup。报告保留这个 IDA function-boundary差异，不把未独立恢复的
fragment伪装成一个额外函数。

## 6. iOS arm64：多入口 Engine unwind helper

iOS arm64正常路径为：

- `0x1001B8034`：Player size `0x4B8`；
- `0x1001B8038`：`operator new`；
- `0x1001B803C`：pending pointer保存在 `X22`；
- `0x1001B8048`：`Player` constructor；
- `0x1001B804C`：发布到 `Engine+0x2B8`；
- `0x1001B8050`：开始分配 size `0x60` 的 position controller。

`EmoteEngine_ctor_unwind_cleanup_guess`（`0x1001B829C`）是一个 224-byte、多入口的
compiler-generated cleanup区域：

- entry-specific pending-allocation branch在 `0x1001B8398` 只删除尚未发布的 raw pointer；
- later completed-prefix branch在 `0x1001B8414` 读取 `Engine+0x2B8`，`0x1001B8418`
  先清 owner slot，`0x1001B8420` 调用完整 `Player` destructor，`0x1001B8424` 再 delete；
- 随后继续清理 Player之前已经完成的 Engine containers，并在 `0x1001B84BC` 恢复原异常。

本端的 exchange/clear-before-destruct只是 libc++/compiler lowering差异；共享源码仍是同一个
pointer-sized owner member。

## 7. iOS armv7：SJLJ call_site与精确分叉

iOS armv7把本切片编码得最明确。`sub_1B7788` 在 `0x1B7844` 注册 SJLJ frame；围绕前三个
frontier写入 1-based `call_site`：

| stored `call_site` | switch case | 受保护阶段 | dispatcher行为 |
|---:|---:|---|---|
| 1 | 0 | Player `operator new` | allocation自身失败，没有 raw pointer；直接清理更早 Engine prefix |
| 2 | 1 | Player constructor | `0x1B7B9E` 取 pending raw pointer，`0x1B7BA4` 只调用 `operator delete`，随后清理更早 prefix |
| 3及以后 | 2及以后 | owner已发布，开始position controller及后续members | 逆序销毁completed owner prefix |

关键正常流：

- `0x1B784C`：Player raw allocation；
- `0x1B785A`：call_site 2下执行 Player constructor；
- `0x1B7862`：constructor返回后发布到 `Engine+0x15C`；
- 随后写入下一 call_site并开始 first controller。

dispatcher `sub_1B7B02` 的 case 1在 raw delete后直接跳过 completed-Player label；后续
controller failure则到 `0x1B7CE6`：读取 `Engine+0x15C`，`0x1B7CF4` 清 slot，
`0x1B7D04` 调用 `Player` destructor，`0x1B7D0A` 调用 `operator delete`，再进入更早
Engine prefix cleanup。这是本轮 failure bifurcation最直接的机器级证明。

## 8. 本地源码对照

当前 `cpp/plugins/motionplayer/EmoteEngine.cpp` 使用：

```cpp
EmoteEngine::EmoteEngine(const tTJSVariant &rmDispatch)
    : _player(new Player(rmDispatch)),
      _ctlPosition(new EmoteVarController(2)),
      // ... six later direct controllers ...
```

逐项对照：

1. `_player` 是 declaration-order中的第一个 direct owner；
2. `new Player(rmDispatch)` 先完整求值，`unique_ptr` member随后才构造/发布；
3. 下一个 initializer恰为 `EmoteVarController(2)`；
4. `Player` constructor failure由 new-expression自动 raw-delete；
5. later member failure由已完成 `_player` member的 destructor自动执行完整
   Player destruction/delete；
6. 无需且不允许给 Engine slot提前手工置 null，也无需 constructor-body `try/catch`。

所以 V261 没有运行时代码修正，只在 initializer附近加入 ABI-neutral注释，明确 pending raw
allocation与completed owner的边界。反编译地址和本端 member offset仍只保留在本报告与 IDB，
没有写进 compiled source。

## 9. recovery IDB 写回

本轮共写回 **15 comments / 15 bookmarks / 0 renames**：

- Android arm64：4处（raw allocation、ctor-before-publish、publication、raw-delete landing）；
- Android armv7：3处（raw allocation、ctor/failure window、publication/next controller）；
- iOS arm64：4处（raw allocation、ctor-before-publish、publication、later completed-owner cleanup）；
- iOS armv7：4处（raw allocation/call_site 1、ctor/call_site 2、publication、case-1 raw delete）。

iOS armv7 different-path安全保存：

- pre-V261 backup：
  `out/idb-recovery/v261-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v261.i64`，
  377,673,936 bytes，SHA-256
  `7D49913540FBAEC727A6608F25A4D59C6256D9705C3BA614D2CA8D839E087B92`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v261.i64`；
- `C:\IDA\idat.exe -A` candidate probe退出 0；
- 覆盖前确认 candidate与 canonical resolved path均位于 workspace；
- candidate与最终 canonical均为 377,673,936 bytes，SHA-256
  `103AB7A20EB11566E0B5364B08C458224617BBC26567F94CB9442B65A170A5D6`；
- canonical重新打开后回读 4条 V261 comments，再关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,728,756 | `1A998CDB075D8B10E0657970EB71D838DA48B380C254BB45AC46B2AC54DEA5C6` |
| Android armv7 | 345,878,941 | `2394D383B1D008442488500B5CDD18F23F18A0F8BE0241A8859330A07188517F` |
| iOS arm64 | 334,901,191 | `0B1706C9B3FB158E51B13BD5A80680EC00E6B484DA825CF97C5C482ECE801CA8` |
| iOS armv7 | 377,673,936 | `103AB7A20EB11566E0B5364B08C458224617BBC26567F94CB9442B65A170A5D6` |

最终 IDA MCP session与本机 IDA process均为 0。

## 10. 验证与产物基线

执行结果：

- ordinary motionplayer test TU syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- V261 comment-only change后的 Web / Wasmtime / guest完整增量构建：通过；
- 最终三目标再次执行均为 `ninja: no work to do`；
- 最终审计期间有一次未加载 EMSDK的 shell触发 Web CMake regeneration失败，并把派生
  cache中的 toolchain path写坏；随后按 `Web Debug Config` preset重新配置成功，受配置生成物
  影响重编译 22项并完成链接，再次三目标 no-work。该事故未修改源码或 IDB；
- `git diff --check` 退出 0；仅打印工作树既有 LF→CRLF warning；
- IDA MCP session 0，IDA process 0。

最终 Wasm：

| wasm | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `index.wasm` | 85,655,322 | `0x1BD31` | `0x1A4109D` | `0x5A3E40` | `0x3185F7B` | `E58F27436614228AD4FC9D13A7F79A13269330FF4FD22209466C214BFC68B260` |
| Wasmtime `index.wasm` | 85,002,463 | `0x1BA50` | `0x19E904B` | `0x5A1090` | `0x3141E11` | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` |
| guest | 151,479,107 | `0x1618E` | `0x13D7DCD` | `0x4D1630` | `0x1421EBA` | `FC8AC9D73004DE8234F47D193385283D232E43E2F39CA9B20149639C23473993` |

三个产物的 size与列出的 section size均保持 V259/V260基线；whole-file hash变化位于带调试/
source映射的重编译产物，V261没有行为代码变更。

## 11. 本轮闭合与后续方向

V261把 V260的 Player内部 partial-construction ladder接到了外层 Engine object graph：
Player构造失败时没有完整对象、没有 owner publication、没有 controller；发布后失败则完整析构
Player。下一切片应沿同一个 constructor继续闭合七个 direct controller的逐个
allocation→constructor→publication frontier、每个控制器的真实参数/对象尺寸，以及第 N 个
controller失败时精确销毁前 N−1个 owner再销毁 Player的逆序阶梯。
