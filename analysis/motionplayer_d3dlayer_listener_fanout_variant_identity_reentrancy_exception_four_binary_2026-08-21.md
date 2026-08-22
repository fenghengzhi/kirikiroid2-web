# MotionPlayer D3DLayer listener fan-out、Variant identity、重入与异常边界四参考闭环（V268）

## 1. 结论

V268 沿 V267 的 list node 生命周期继续闭合所有主要 listener consumer：

- `D3DLayerObject::OnUpdate`；
- `D3DLayer::setMatrix` / `setMatrixGL` 的 matrix-change notification；
- `D3DLayer::Draw`。

四参考一致证明：这些路径都直接遍历 live `std::list`，iterator 的 next link 只在 callback
返回后才从当前 node 读取。没有 snapshot、next prefetch、reentrancy flag、deferred erase、
exception catch 或 continuation。

本轮同时发现并修正一个真实 portable 偏差：参考 `OnUpdate` 不复制 `state` Variant，只把
来参地址直接放入 `tTJSVariant *parameters[1]`。旧 portable 构造了临时 Variant，额外产生
copy/AddRef/Release/destructor 和 EH cleanup surface；现已恢复成原指针零拷贝转发。

## 2. 函数地图

| 目标 | OnUpdate | setMatrix | setMatrixGL | D3DLayer::Draw |
|---|---:|---:|---:|---:|
| Android arm64 | `0x529B9C` | `0x52D578` | `0x52D628` | `0x533624` |
| Android armv7 | `0x492308` | `0x4937AE` | `0x49383E` | `0x496EC6` |
| iOS arm64 | `0x100230140` | `0x1002319E4` | `0x100231A74` | `0x100235AAC` |
| iOS armv7 | `0x22F23E` | `0x2307FE` | `0x230892` | `0x2348B2` |

listener 虚槽在 LP64/ILP32 上分别是：

| callback | LP64 vtable byte offset | ILP32 vtable byte offset |
|---|---:|---:|
| `IsVisible()` | `+0x10` | `+0x08` |
| `Draw(target)` | `+0x18` | `+0x0C` |

## 3. OnUpdate 的原 Variant identity

四端入口都是：

```cpp
bool OnUpdate(tjs_int updateState, const tTJSVariant &state);
```

`updateState` 不参与函数体，但仍是实际虚函数 ABI 参数。四端对第三个 machine argument 的
第一项处理分别是：

| 目标 | 原 state pointer store | script FuncCall |
|---|---:|---:|
| Android arm64 | `0x529BC0: STR X2,[SP]` | `0x529BF8` |
| Android armv7 | `0x49231E: STR R2,[SP+...]` | `0x492340` |
| iOS arm64 | `0x100230158: STR X2,[SP+...]` | `0x100230190` |
| iOS armv7 | `0x22F246: STR R2,[SP+...]` | `0x22F274` |

随后把这个 stack slot 的地址作为 `tTJSVariant **param`、`numparams=1` 传给：

```cpp
ScriptOwner->FuncCall(
    0, TJS_W("onUpdate"), nullptr, nullptr,
    1, parameters, ScriptOwner);
```

不存在任何 Variant constructor/copy/dtor call；四端也没有本地 Variant cleanup landing。
因此 source shape 必须是：

```cpp
tTJSVariant *parameters[] = {
    const_cast<tTJSVariant *>(&state)
};
```

而不是 `tTJSVariant parameter(state)`。由此得到的精确边界：

1. `OnUpdate` 自身不改变 state closure 的 refcount；
2. `FuncCall` 同步收到与 caller 传入对象完全相同的 Variant 地址；
3. mutable `tTJSVariant **` ABI 技术上可通过该地址改写被声明为 const-reference 的对象；
4. `FuncCall` 的普通 `tjs_error` 返回值被忽略，无论成功/失败码都继续 fan-out；
5. 真正抛出的异常直接退出，不初始化 listener cursor；无本地 Variant 需要析构。

## 4. OnUpdate 的 callback 顺序

script call 返回后才读取 list：

| 目标 | first cursor | listener call | result OR | post-callback next |
|---|---:|---:|---:|---:|
| Android arm64 | `0x529BFC` | `0x529C18` | `0x529C20` | `0x529C1C` |
| Android armv7 | `0x492342` | `0x492350` | `0x492354` | `0x492352` |
| iOS arm64 | `0x100230198` | `0x1002301B4` | `0x1002301B8` | `0x1002301BC` |
| iOS armv7 | `0x22F276` | `0x22F286` | `0x22F28A` | `0x22F288` |

Android 两端和 iOS armv7 的 optimizer 把 next load 排在 OR 前，iOS arm64 把 OR 排在
next load 前；但四端 next 都严格在 callback 返回后。`result` 初值为 false，每个 duplicate
node 都调用，OR 不短路，正常结束只返回累计值的 bit 0。script callback 的返回值和
`tjs_error` 都不参与这个 bool。

script callback 在 first cursor 之前，因此它添加/删除的 listener 会被随后遍历看到；若它
销毁 layer/self，则 first cursor load 已经是悬空对象访问。

## 5. live-list mutation 状态矩阵

假设 callback 返回且 current node 自身保持有效：

| callback 内 mutation | 返回后的行为 |
|---|---|
| 删除已经访问的 earlier node | current next 不受影响，继续 |
| 删除 future node | post-callback current.next 已 relink，跳过被删 node |
| tail append | 新 tail 在抵达 sentinel 前被同一轮访问 |
| 每个新 callback 再 append | 本轮可以无限延长/不终止 |
| 删除与 current 相同 payload | `RemoveListener` remove-all 删除 current 及 duplicates；随后从 freed current 读 next，UAF/UB |
| clear/destroy layer | current/list sentinel 失效；返回后 UAF/UB |

portable 回归只覆盖定义良好的两项：在 current 保持存活时删除 next/future node，并 append
一个此前不在 list 中的 listener；结果是 removed listener 不调用、retained listener 调用、
appended listener 同一轮调用。self-removal UAF 只记录，不把未定义行为写成不稳定测试。

## 6. matrix notification、重入和异常

`setMatrix` / `setMatrixGL` 四端都先完整调用 `Mat4::set`，再初始化 listener cursor。
notification 逐个调用 `IsVisible()` 并丢弃返回值；next 仍在 callback 返回后读取。

因此：

- callback 抛出时后续 listener 不执行；
- 已提交 Matrix 不回滚；
- callback 重入另一次 matrix setter 时，内层调用覆盖 Matrix 并完整运行自己的 fan-out；
- 外层返回后继续其 live cursor，但不会恢复外层 Matrix，后续 listener 读取的是重入后的状态；
- 没有 recursion depth guard，恶意重入可以栈溢出。

回归覆盖 `setMatrixGL` 在首 callback 抛出时：后续 listener call count 不变，而 Matrix 已是
完整的新 column-major 映射。

## 7. Draw 的 target sampling

`D3DLayer::Draw` 先检查一次 `Parent != nullptr` 与 `Visible != false`。满足时读取一次 parent
中的 `CurrentTarget`，并把同一个缓存 pointer 传给每个 listener `Draw(target)`。

Android arm64 在检查 list 是否为空前就读取 target；其余三个 lowering 在 nonempty check 后
读取，但都在首 callback 之前。source-level 行为一致：callback 中改变 parent/current target
不会刷新传给后续 listener 的 pointer；把原 target 销毁则会让后续 callback 收到悬空 pointer。

Draw callback 抛出立即退出；next 不读取、后续 listener 不执行。正常返回后再读取 live next，
所以 mutation/UAF 矩阵与 IsVisible fan-out 相同。

## 8. Android trap/function-boundary 旧账纠正

canonical recovery IDB 的 fresh readback 推翻了旧报告中“已经拆分”的状态声明：

- Android arm64 仍把 `0x529B98` 的 `BRK #1` 和随后 `0x529B9C..0x529C60`
  OnUpdate 合成 `sub_529B98`；
- Android armv7 的 `0x492304` 仍不是 function。

V268 实际修复后：

- arm64 为 `0x529B98..0x529B9C` 一指令 trap 与
  `0x529B9C..0x529C60` OnUpdate；
- armv7 原始 bytes `FE DE 00 00` 正确解码为 `0x492304..0x492306`
  2-byte `UDF #0xFE`、随后 `0x492306..0x492308` 2-byte
  `MOVS R0,R0` padding；不是旧注释中的 `UDF #0xDEFE`。

arm64 因原函数已跨越两段，原生 define helper 无法缩短边界；本轮在独立 candidate 上用最小
IDAPython `del_func/add_func` 拆分，`idat -A` 后再发布。armv7 可直接恢复 code/function。

## 9. 源码与回归改动

`cpp/plugins/DrawDeviceD3D.cpp`：

- 删除 `OnUpdate` 的临时 `tTJSVariant parameter(state)`；
- 用 `const_cast<tTJSVariant *>(&state)` 恢复原地址参数数组；
- 补充 OnUpdate/matrix/Draw 的 live next、异常与 target-cache 精确注释。

`tests/unit-tests/plugins/motionplayer-dll.cpp`：

- recorder 记录 `param[0]` 的实际地址，断言与 caller `state` 地址相同；
- 新增 future erase + tail append 同一轮可见性回归；
- 新增 matrix commit-before-callback-exception/no-rollback 回归。

## 10. Recovery IDB 写回

四库合计：

- 80 条 function/line comment；
- 18 个 bookmark；
- 22 个 `_guess` function rename；
- 22 个 function type/prototype update；
- 22 次定向 force-recompile/readback；
- Android arm64 一个 merged-function split；
- Android armv7 一个 code/function boundary recovery。

| 目标 | comments | bookmarks | renames/types | force readback | final bytes | final SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Android arm64 | 20 | 5 | 6 / 6 | 6 | 366802442 | `1F33BC73FEB2237B3F369C631CF329DE3FB368AF43DA143B24E04E4284D357D1` |
| Android armv7 | 20 | 5 | 6 / 6 | 6 | 346007108 | `C484A98CBF18CB6A8815A4EADBE6E2FAC9A9BB76023344B6F5F30FBF0ECFC65F` |
| iOS arm64 | 20 | 4 | 5 / 5 | 5 | 335015846 | `DF25BFFDDFAC00E7FC775C8FE99C2D0D6D7D739D007AC986CEE64860CAFDF741` |
| iOS armv7 | 20 | 4 | 5 / 5 | 5 | 376909668 | `BBA35C6359167F8F2E53B8715A10C90031A96BFB062C83C77D30939529E8E93D` |

四个 canonical IDB 最后均独立通过 `C:\IDA\idat.exe -A`。Android arm64 function-split
candidate 与 iOS armv7 edit candidate 已从最终 canonical 刷新，size/hash 分别与最终表逐字节
一致；iOS armv7 candidate 编辑前、编辑后都通过 `idat -A`，发布后再从 canonical 独立重开，
fresh decompile OnUpdate/Draw/setMatrix/setMatrixGL 并 `save=false` 关闭。

## 11. 验证状态

验证结果：

- 完整 `motionplayer-dll.cpp` ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 syntax-only
  均 exit 0；这包含原 Variant 地址 identity、future erase + same-pass append 和
  matrix-exception/no-rollback 三组新增断言；
- Web 首次构建在进入编译前触发 CMake regenerate，暴露旧 cache 中错误的
  `CMAKE_TOOLCHAIN_FILE=/upstream/...`；用现有 `Web Debug Config` preset 把它恢复成完整
  EMSDK 路径后，Web 24/24 成功；这是 build-tree 配置旧账，不是源码编译失败；
- Wasmtime 4/4、Wasmtime guest 1/1 成功；
- Web、Wasmtime、guest 随后顺序复跑均为 `ninja: no work to do`；
- 两个当前 preset 均未注册测试 executable；`ctest --output-on-failure` 都 exit 0，但准确报告
  `No tests were found!!!`，所以不能把 syntax-only 回归表述成已运行的 Catch2 test；
- `llvm-nm` 保留 `D3DLayerObject::OnUpdate(int,tTJSVariant const&)`，定向 `llvm-objdump`
  显示函数把来参 state pointer 直接 store 到参数数组再 `call_indirect`，没有 Variant copy call；
- `git diff --check` exit 0，仅有工作树既有 LF→CRLF conversion warning；
- compiled source/test 的本轮 reference absolute-address 扫描无命中；
- `mcp__idalib__idb_list` 为 `sessions=[]`, `count=0`，进程审计也没有残留
  `ida`/`ida64`/`idat`/`idat64`/`idalib-worker`。

最终 Wasm：

| 产物 | bytes | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85655262 | `E457AC2188ECE5865114CEA805880538C440EB8FB238232368DE6C6E52CFA351` |
| Wasmtime `index.wasm` | 85002403 | `D6BA87DA418A40816221CFC92C994E93B362E81FFB570F61D71226C728C50BBB` |
| Wasmtime guest | 151479033 | `C6B427370B0B555E2E131948119BA1A3B7F2630F8670D16C22C7051C61C95C2F` |

相对 V267 的 section delta：

| 产物 | total bytes delta | CODE V267 → V268 | DATA | name | 其他变化 |
|---|---:|---|---|---|---|
| Web | `-60` | `0x01A4109D → 0x01A41061` (`-0x3C`) | `0x005A3E40` 不变 | `0x03185F7B` 不变 | 无 |
| Wasmtime | `-60` | `0x019E904B → 0x019E900F` (`-0x3C`) | `0x005A1090` 不变 | `0x03141E11` 不变 | 无 |
| guest | `-65` | `0x013D7DCD → 0x013D7D9B` (`-0x32`) | `0x004D1630` 不变 | `0x01421EBA` 不变 | `.debug_info 0x02C0E136 → 0x02C0E127` (`-0x0F`) |

guest 的 `.debug_abbrev/.debug_ranges/.debug_str/.debug_line/.debug_loc/.debug_aranges` size
均不变。两份主产物对称减少 60 B、且只有 CODE size 改变，与删除临时 Variant 的
copy/destructor 路径一致；这是有意的 executable fidelity 修复，不是注释/DWARF-only 变化。

## 12. 闭合范围

V268 已闭合 OnUpdate state identity、script/fan-out 顺序、三个 listener consumer 的 live iterator、
定义良好的 mutation 可见性、self-removal UAF、matrix reentrancy/no-rollback、Draw target cache、
callback exception propagation，以及两个 Android trap/function-boundary 旧账。

这不表示 motionplayer 全目标完成；下一纵切面继续沿尚未闭合的调用链、容器、owner 与边界行为推进。
