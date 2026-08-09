# Follow-up：150-entry landing-pad cleanup contract surface

`LANDING-PAD-CLEANUP-CONTRACT`

## 结论

- 权威 Android ARM64 `libkrkr2.so` 的 39 张 LSDA 表含 155 条非零 landing 引用；5 个
  cleanup landing 被两个 guarded range 复用，因此折叠为 **150 个唯一 landing**。
- 150 个唯一入口精确分为 **75 cleanup-only + 75 catch-all**。沿 ARM64 显式控制流展开后：
  - 75/75 cleanup-only 全部以 `_Unwind_Resume` 结束；
  - 72/75 catch-all 只有一条 `BL clang_call_terminate_guess@0x520FAC`；
  - 其余 3/75 是 catch/delete/rethrow：Factory 的 `0x598190`、`0x5981A0`，以及
    `std::vector<std::string>` 扩容 helper 的 `0x59B99C`。
- 所有 landing 显式可达指令与函数入口显式可达正常流 **0 overlap**。异常清理没有跳回
  正常业务块，也没有隐藏的“成功继续执行”边。
- 当前源码的 automatic Variant/ttstr/raw-node/vector/ncbind delegate、显式 Factory
  `catch (...)`、stream RAII 与故意保留的 raw-data 异常泄漏边界逐层一致；本轮未发现新的
  `cpp/` GAP，因此没有修改生产代码。

本轮只使用 Android ARM64 二进制、IDA MCP 与仓库当前源码；没有使用 Android ARMv7、
iOS ARMv7、旧私库或同版本源码。

## 为什么不能直接使用 IDA FlowChart reachability

IDA 的 FlowChart 会把 LSDA 异常边作为 predecessor/successor 暴露。例如 Factory 的
`0x598144`/`0x59814C` guarded blocks 分别把 `0x5981A0`/`0x598190` 列为 successor；若从
函数入口直接 DFS，就会把 catch body 误标为正常流。反过来，cleanup 内可能抛出的析构
调用也会把 paired terminate landing 混入同一个闭包。

本轮改为从 ELF 指令直接解码，仅跟随机器码中的显式边：

1. `B/BL`、`B.cond`、`CBZ/CBNZ`、`TBZ/TBNZ`；
2. 42 张既有 switch 表给出的 `BR` 目的地；
3. `BLR` 的正常 fallthrough；
4. `RET/BRK/HLT/ERET` 与已证明的 noreturn callee 作为终点；
5. 不注入任何 LSDA exception arc。

同一解码分别从函数入口和每个 landing root 开始；150 个 landing 闭包均与正常闭包
不相交。该方法既保留共享 cleanup DAG，又不会把“析构再次抛出”的 paired action-1
landing 当作当前析构的正常后继。

## 全量机器面

| 项目 | 结果 |
| --- | ---: |
| LSDA owner | 39 |
| 非零 landing 引用 | 155 |
| 唯一 landing | 150 |
| cleanup/resume | 75 |
| direct terminate | 72 |
| catch/rethrow | 3 |
| 唯一显式可达指令 | 569 |
| 按 150 个 root 重复计入的 contract 指令 | 1,150 |
| 单 root 最少/最多指令 | 1 / 63 |
| 唯一 transfer site | 168（162 `BL` + 6 `BLR`） |
| 正常流交集 | 0 |
| canonical CFG SHA-256 | `e4c2c6f0f019cfdefd8489005dd0bef910a873103cf53dacd850ffb471aea480` |

80 条 cleanup call-site 引用折叠成 75 个 root 的 5 处复用是：`0x597530`、`0x5986C4`、
`0x5986C8`、`0x59A49C`、`0x59B114`。它们的多个 guarded range action 都为 0；不存在
同一 landing 混用 cleanup/catch-all action。

### landing 闭包内的唯一直接调用站点

| target | 唯一站点 | 清理语义 |
| --- | ---: | --- |
| `__cxa_guard_abort@0x4013C0` | 1 | local-static 初始化失败 |
| `__cxa_begin_catch@0x408BD0` | 2 | Factory/vector 的 3 个 root 共用 2 个 catch body |
| `__cxa_rethrow@0x4139B0` | 2 | 两个 catch body 重新抛出 |
| `operator delete@0x415740` | 12 | holder/owner/vector 等对象存储 |
| `_Unwind_Resume@0x41A950` | 28 | 75 个 cleanup root 共用 28 个终点 |
| `__cxa_end_catch@0x422530` | 2 | rethrow 自身异常的 paired cleanup |
| `clang_call_terminate_guess@0x520FAC` | 72 | destructor/deallocator 再次抛出时 terminate |
| `RegistEnd_guess@0x59AD84` | 1 | AutoRegister::Regist 展开失败后的结束阶段 |
| `TVPGetScriptDispatch_guess@0x8E3C20` | 1 | AutoRegister::Unregist cleanup |
| `tTJSNarrowStringHolder_dtor_guess@0x9B1EAC` | 2 | dictionary key narrow holder |
| `TJSAlignedDealloc_guess@0xA0DE90` | 5 | terminal raw-owner data 释放 |
| `tTJSVariant_dtor_guess@0xA0F778` | 14 | 完整 Variant 自动对象析构 |
| shared ref release `0xA13274` | 16 | ttstr/raw holder 等共享引用释放 |
| vector/storage delete thunk `0x14A3C0C` | 4 | `std::vector` backing storage |

6 个 `BLR X8` 分别是：`Load@0x598458/0x59848C` 与 `EnsureContainer@0x59A088` 的
`std::function` manager cleanup、`LoadStorage@0x5986E0` 的 stream deleting destructor，
以及 `AutoRegister::Unregist@0x59AA48/0x59AA58` 的两层 dispatch Release。producer word、
寄存器与角色仍由既有 indirect-ABI surface 独立固定。

## 三个非 terminate catch-all

### Factory：`0x598190` / `0x5981A0`

两个 action-1 root 进入同一源码 catch，但 live-object 集合不同：

```text
0x598190: destroy copied by-value Variant -> join 0x5981A4
0x5981A0:                                  join 0x5981A4
0x5981A4: __cxa_begin_catch(exception)
           if published file->owner reaches zero:
               TJSAlignedDealloc(owner->data); operator delete(owner)
           operator delete(published file)
           __cxa_rethrow()
```

这精确对应 `main.cpp:732-748`：先构造并发布 `PSBFile *`，`try` 内按值复制首参数并调用
`Load`；`catch (...)` 删除已发布 holder 后原样重抛，且不清空 `*result`。若 catch/rethrow
路径本身再次抛出，`0x5981E4` 执行 `__cxa_end_catch -> _Unwind_Resume`，其 paired
`0x5981F4` 才是 terminate。

### vector 扩容：`0x59B99C`

```text
__cxa_begin_catch(exception)
if newBuffer == null: BRK #1       // 编译器不可能状态
operator delete(newBuffer)
__cxa_rethrow()
```

这是 `GetDictionaryKeys` 的 `result.emplace_back(key)` 所调用的
`std::vector<std::string>::_M_emplace_back_aux` 内部强异常保证，不是 psbfile 自定义异常
处理。`0x59B9B4` 是 `__cxa_end_catch -> _Unwind_Resume` cleanup；`0x59B9C4` 是其 paired
terminate。当前 `PSBRawFile.cpp:280-306` 继续保留 `std::vector<std::string>`、复用
`std::string key` 与 `emplace_back`，没有用手写容器替代。

## 代表性 cleanup-only 与源码生命周期对照

| 二进制 cleanup | 源码结构/边界 |
| --- | --- |
| `CreateVariant/EnumMembers` 的多层 `tTJSVariant_dtor` 链 | `main.cpp` 保留具名 Variant 数组/成员临时量，异常展开按逆声明顺序析构。 |
| `LoadStorage@0x5986C0..0x5986E8` 的 stream v-dtor 后 resume | `PSBRawFile.cpp:482-513` 用 stream RAII；`ReadBuffer` 抛出时只销毁 stream，`data` 仍是 raw pointer，故按二进制故意泄漏。 |
| `LoadStorage@0x5986EC` 先 shared-ref release，再回跳共享 stream cleanup | placed-path `ttstr` 仍 live 的 guarded range 比 stream-only root 多一层字符串释放。 |
| `GetDictionaryKeys/GetListAt` 的 string/vector storage delete 链 | `PSBRawFile.cpp:280-306` 与 `PSBMedia.cpp:149-219` 保留 reusable `std::string`、vector 与 packed view 的作用域。 |
| `EnsureContainer` 的 Variant/shared-ref/owner cleanup | `PSBMedia.cpp:19-49` 保留 `container`、raw `file`、`nextFile` 与 adaptor ownership；没有用 shared_ptr 合并层次。 |
| `Resolve@0x59A824..0x59A8C8` 的 holder → segment/rest → current owner | `PSBMedia.cpp:52-109` 保留 `current`、`rest`、内层 `segment`、唯一 narrow holder 与 strict-get 临时量。 |
| `RegistItem/RegistEnd/AutoRegister` 的临时字符串、Variant、delegate cleanup | `ncbind.hpp:1853-1925,2148-2157` 保留原模板 delegate、`RegistBegin/Item/End` 三阶段与自动局部量。 |
| `CopyFirstArgument@0x59B7B0..0x59B7E4` 的 1/2 个 Variant 析构层 | ncbind typed wrapper 继续按值复制第一个 TJS 参数并通过模板调用链交给 `PSBFile::Load`。 |

## verifier gate

`verify_elf_surface.py` 现直接从权威 ELF：

1. 重新解析 39 张实际 LSDA 表并折叠唯一 landing/action；
2. 从每个 owner 入口与 landing root 独立解码 ARM64 显式 CFG；
3. 拒绝 landing/正常流交叉、越出 FDE、action 混用或未知 action-1 类别；
4. 固定 569 个唯一指令、1,150 个 per-root 指令实例、全部 successors 与 168 个 transfer；
5. 固定 14 个 direct target 的站点计数、6 个 BLR 站点/寄存器/角色和 3 个
   catch/rethrow root；
6. 对 canonical `(owner, landing, action, instruction, word, successors)` 序列校验 SHA-256。

当前输出：

```text
landing_pad_contract_surface=true owners=39 landings=150 cleanup_resume=75 terminate=72 catch_rethrow=3 unique_instructions=569 contract_instructions=1150 unique_transfers=168 direct_targets=14 blr_roles=3 normal_overlap=0 sha256=true
```

复现：

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-dwarfdump
```

## IDB 与实现状态

- fresh decompile：`0x520FAC`、`0x5980F4`、`0x598538`、`0x59A4B0`、`0x59AEEC`、
  `0x59B708`、`0x59B7E8`；复杂路径另用逐指令 CFG 交叉核实。
- 已给 terminate helper、Factory 两个 catch root/paired cleanup、LoadStorage stream cleanup、
  Resolve layered cleanup、RegistItem、CopyFirstArgument 与 vector catch/rethrow 写入
  `LANDING-CLEANUP-CONTRACT` IDB 注释并保存。
- 结论是 `HAS_GAP: 0`；没有修改 `cpp/`，因此本轮不触发 Web 构建。
