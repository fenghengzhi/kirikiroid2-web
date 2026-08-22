# MotionPlayer `tTVPDrawDevice` 析构 manager 快照、Release 重入与双 storage teardown 四二进制审计（V278）

日期：2026-08-22  
范围：共享 `tTVPDrawDevice::~tTVPDrawDevice()`、Managers pointer-vector copy/destruction、manager
`Release` callback 重入，以及 concrete root 的 secondary-before-primary base destruction。  
参考：Android arm64-v8a、Android armeabi-v7a、iOS arm64、iOS armv7 四份当前
`reference/binaries/` canonical recovery database。  

## 1. 本轮结论

四份参考共同实现：

```cpp
std::vector<iTVPLayerManager *> backup = Managers;
for(auto *manager : backup)
    manager->Release();
// destroy backup pointer storage
// destroy the then-current Managers pointer storage
```

这个 snapshot 只稳定迭代 storage，不稳定 pointee 生命周期，也不建立额外 ref：

- raw pointer copy 不 AddRef；
- Release callback 可修改 live `Managers`，但 backup cursor/count 固定；
- callback 新 append 的 manager 不在 backup 中，最终只随 live vector storage 一起丢掉 pointer，
  对应 AddRef 不会由本析构配对；
- callback Remove/Release 尚未遍历的 manager 可提前销毁对象，稍后的 backup entry 成为 UAF；
- callback Remove 当前 manager 会产生 snapshot Release + base Remove Release 两次调用；
- backup 结束后不重新扫描 live vector，也不对 live elements 调 Release，只 raw-free当前 storage。

vector copy 还有一个隐藏的双快照边界：第一次 source begin/end 差决定 exact allocation capacity；
allocation 返回后重新读取 source begin/end，第二差决定 memcpy 长度和 backup end。普通 operator new
通常不重入，但 allocator hook 若增长同一个 Managers，第二长度可以超过第一次容量而溢出；缩短则
只复制新的短前缀。

complete root destructor 四端都先调用 secondary `tTVPDrawDevice` destructor，再调用 primary
root destructor。manager Release 期间 root trees/items 仍存在，但 vptr 已降级到 base draw-device；
经虚接口重入 Remove 只能进入 base Remove，不会清 manager data 或删除 plugin item。随后 primary
destructor 只销毁 tree nodes，不 delete stored item pointers。

## 2. 四端入口与 helper

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `tTVPDrawDevice` complete dtor | `0xA72970` (`0x12C`) | `0x796E6C` (`0x70`) | `0x1002DC0F4` (`0xBC`) | `0x2DBB0C` (`0xD4`) |
| concrete root complete dtor | `0x531410` | `0x495744` | `0x100233F54` | `0x232C74` |
| pointer-vector copy helper | inline | `0x796F04` | `0x1002DCEE4` | `0x2DC63C` |
| pointer-vector storage dtor | `0xA72960` | inline | `0x1000D3908` | `0x2DC610` |
| outer destructor cleanup | inline landing | — | — | `0x2DBBE0` |
| copy-helper cleanup | — | — | — | `0x2DC6F0` |

恢复 prototype：

```cpp
void tTVPDrawDevice__complete_dtor(void *self);
void manager_vector_copy_ctor_guess(void *dst, const void *src);
void manager_vector_storage_dtor_guess(void *vector);
```

iOS armv7 的两个 compiler SJLJ landing 保留 `_guess` 名，不为其反编译出的 frame 参数伪造源码
prototype。

## 3. complete root 的基类销毁顺序

四端 complete root body 都只有同一调用链：

| 目标 | secondary 调用 | primary tail-call |
|---|---:|---:|
| A64 | root `+0x178` -> `0xA72970` | `0x53244C` |
| A32 | root `+0xD4` -> `0x796E6C` | `0x49606C` |
| I64 | root `+0x118` -> `0x1002DC0F4` | `0x100233E1C` |
| I32 | root `+0xA4` -> `0x2DBB0C` | `0x232B14` |

这是声明 `class DrawDeviceObjectBase : Primary, tTVPDrawDevice` 的逆序 direct-base destruction：

1. secondary base 安装自己的 vptr；
2. snapshot/release managers；
3. 释放 live manager vector storage；
4. primary base 才释放 targets、transition texture、owned Modules 和四棵 tree 的 nodes。

所以 manager Release callback 看到的 root primary state 仍在，但 draw-device virtual dispatch 已是 base。
如果 callback 通过 `iTVPDrawDevice*` 调 `RemoveLayerManager`，不会再进入 plugin 的 data-first item
teardown。primary destructor 也只把 FrontItems/BackItems nodes 当 borrowed pointer nodes 释放，不能
补救 manager data/item allocation。

## 4. backup copy 的精确边界

四端 copy 都构造 `begin/end/capacity-end` 三指针目标，容量恰好等于第一次观察到的 source size。
pointer 元素使用 raw memcpy/memmove，没有 element constructor、AddRef 或 null 检查。

精确阶段：

```text
size1 = source.end - source.begin
backup.allocate_exact(size1)
begin2 = source.begin
end2   = source.end
size2  = end2 - begin2
memcpy(backup.begin, begin2, size2)
backup.end = backup.begin + size2
```

由此：

- source 初始 empty：不分配，backup begin/end/cap 均 null；
- allocation failure：没有 Release 开始；平台 EH 决定是否清 live storage/terminate；
- allocation hook 使 source grow：`size2 > size1`，copy 可越过 backup allocation；
- allocation hook 使 source shrink：只复制当前短前缀，capacity 保持较大；
- allocation hook reallocate：第二 begin/end 指向新 storage，因此不会继续复制旧 freed begin；
- source 中 duplicate/null 原样复制；duplicate 被重复 Release，null 在 vptr load 崩溃。

Android A64 内联 copy，A32 helper 使用 libstdc++ layout；两个 iOS helper 使用 libc++ layout，但上述
双读取/精确容量完全一致。

## 5. fixed backup Release 与 live vector 重入

Release loop 的 backup begin/end 在进入遍历后固定，不读取 live Managers。每个 entry 严格按原
snapshot 顺序调用 vtable Release；没有 null、generation、still-present 或 refcount guard。

| callback 对 live Managers 的操作 | snapshot 后果 |
|---|---|
| 无操作 | 每个 snapshot pointer Release 一次 |
| append new manager | 新 pointer 不在 backup；析构末尾 storage 被 free，但新 AddRef 不配对 |
| Remove 当前 manager | base Remove 再 Release 一次并 erase live entry；outer snapshot 不受 iterator 影响 |
| Remove later manager | later live entry Release/erase；backup 仍保留旧 pointer，可能稍后 UAF Release |
| clear/reallocate live vector | backup storage 仍稳定；最终析构 current live storage，不再 Release其元素 |
| destroy manager without live erase | backup 后续 duplicate/alias pointer 可 UAF |
| Release escape | 后续 snapshot entries 不访问；平台 cleanup/terminate 矩阵生效 |

snapshot vector 自身不 owns pointee。它只保证 manager callback 调 Remove 时不会直接破坏 loop cursor，
不能保证 callback refcount 操作后 pointer 仍活着。

## 6. 双 storage teardown

normal path 总是：

1. 遍历完成后把 backup end 退到 begin（pointer destructor trivial）；
2. raw-delete backup begin；
3. 对 **当前** live Managers 做相同 trivial-element teardown；
4. raw-delete live begin。

第三步读取的是 callback 修改后的 current begin/end，不是进入析构时保存的 original storage。
vector destructor 不对 pointer 元素调用 `Release`，所以 post-snapshot appends 明确丢失 owner release。
字段通常不必为即将结束的 subobject 清零；不能把 raw storage free 误标成 manager object free。

## 7. EH 平台矩阵

| 目标 | copy escape | manager Release escape |
|---|---|---|
| Android arm64 | outer noexcept landing 销毁 live Managers storage后 terminate | 先 free backup，再销毁 live storage、terminate |
| Android armv7 | 无本地 cleanup landing；可越过 backup/live storage与primary dtor | 同左 |
| iOS arm64 | 无本地 cleanup landing；可越过 backup/live storage与primary dtor | 同左 |
| iOS armv7 | copy helper先清 partial backup并resume；outer再清 live storage、terminate | outer action清backup、清live storage、terminate |

iOS armv7 outer dtor 在 copy 前设 SJLJ call-site 1，在每次 Release 前设 call-site 2：

- copy-failure action 没有 completed backup 要 free；
- Release-failure action raw-destroy backup；
- 两个 action 汇合到 live vector storage destructor，再调用既有
  `clang_call_terminate_guess`；
- unexpected selector 的 `UDF` 不是业务路径。

A64 使用 DWARF landing 表达相同 cleanup。A32/I64 没有 local landing；不能从 A64/I32 外推这两端
也完成两份 storage cleanup。

## 8. 源码与旧报告

- `cpp/core/visual/impl/DrawDevice.cpp`：保留 upstream 的 `backup = Managers; Release loop`，新增
  raw-pointer/non-owning snapshot、new append omission、later-pointer UAF 和平台 EH 注释；
- `cpp/plugins/DrawDeviceD3D.cpp`：在默认 root destructor 旁记录 secondary-before-primary 次序以及
  manager phase 不清 plugin item/data；
- `analysis/motionplayer_drawdevice_manager_attach_detach_container_lifecycle_four_binary_2026-08-15.md`：
  旧“copy 后 Release”小节已补双 size sample、pointee/reentry 与 EH 边界。

没有增加第二轮 Release、临时 AddRef、data clear、item delete、callback lock 或 defensive null check。

## 9. 构建与 Wasm

- ordinary/headless 两种 `motionplayer-dll.cpp` syntax-only 均 exit 0，仅既有 `_tss` warning；
- Web、Wasmtime main、guest 成功重编/链接，随后三目标均 `ninja: no work to do`；
- 两个 CTest 目录 exit 0，均为 `No tests were found!!!`；
- 三份 Wasm validate/Module 构造成功，imports/exports 不变；
- Wasmtime core object 的 `tTVPDrawDevice::~tTVPDrawDevice` 定向反汇编确认：安装 base vptr、
  vector copy、固定 iterator loop/vslot Release、backup vector dtor、live vector dtor；整个 destructor
  在当前 Wasm compiler 下由外层 terminate catch 包围。

本轮仍只有注释变化。Web/main 总大小与 CODE/DATA 等 payload 长度不变但 debug/custom metadata
令 hash 改变；guest 总大小相对 V277 增加 4 bytes，选定 section 长度保持一致。

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85655133 | `539 / 69` | `FD139D3AB7743333FC14F1F10FE372A942136F69DFF5AAC3CBA13D1427DC482F` |
| Wasmtime `index.wasm` | 85002274 | `538 / 69` | `FC2A9119DE514361BEF0FAA346FF181CF80F8B364B23801BC484C759122865AB` |
| Wasmtime guest | 151508375 | `445 / 87` | `0BA130E85592682C8B981236ED34CBD1EB8F94F2E650B7E4DD03B44F70407721` |

| 产物 | FUNCTION | GLOBAL | CODE | DATA | `.debug_str` |
|---|---:|---:|---:|---:|---:|
| Web | `0x1BD30` | `0xD5C2` | `0x1A40FFF` | `0x5A3E40` | — |
| Wasmtime main | `0x1BA4F` | `0xD5EA` | `0x19E8FAD` | `0x5A1090` | — |
| guest | `0x16190` | `0xB1C3` | `0x13D7E10` | `0x4D1630` | `0x1530816` |

相对 V277 总大小为 `+0 / +0 / +4` bytes。

## 10. recovery IDB 写回

| 目标 | comments | bookmarks | renames | types | write readbacks |
|---|---:|---:|---:|---:|---:|
| A64 | 10 | 3 | 2 | 2 | 3 |
| A32 | 10 | 3 | 2 | 2 | 3 |
| I64 | 11 | 4 | 3 | 3 | 4 |
| I32 | 12 | 5 | 6 | 4 | 6 |
| 合计 | 43 | 15 | 13 | 11 | 16 |

iOS armv7 candidate 另做 6 函数 fresh readback，发布后 canonical 独立 auto-analysis/save；最终四库
再次 cold-open，共回读 16 个函数。candidate 发布前 packed hash 为
`67FA1D983624A8BB2890F3F7A1B2447063B9AE6E3A52C45B6393F217E76CFAAF`；canonical 独立 save 后
metadata/hash 不同，语义 readback 一致。

最终 canonical IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368547291 | `0A36D77B35BAD7FF24A3B69B62718F0027A287578CE1592472C76D951C837445` |
| Android armv7 | 346739012 | `EF8EEEC98F0E4DD7CBE181806E243D518A0B22DE96A817E9B6B7EF4CE4C221A8` |
| iOS arm64 | 336228210 | `1AE3E03F78B45ABE7976CDCE53A46F906A31C305A6DA615A56175D5EA2341453` |
| iOS armv7 | 377376618 | `0D1C6C8B238121F92CAD75A0B7542272FEF0F254B5A6E9FD11E71B34EA76B364` |

pre-V278 backups/candidate：

`out/idb-recovery/v278-drawdevice-destructor-manager-snapshot/`

| 目标 | pre bytes | pre SHA-256 |
|---|---:|---|
| Android arm64 | 368547291 | `690A7B48E5D1522A5C67D26E4FE6583049F666920DB1694FFD857B0B5AB96875` |
| Android armv7 | 346739012 | `A3058CFB6DB5C15435A6CFBEE889E92C88C6DF5FE830183F92F11E407976307B` |
| iOS arm64 | 336228210 | `C605FAEA624F2BFDC6AA63950FD16905CB1468712C2FF6D6087DDDB21F8944EA` |
| iOS armv7 | 377376618 | `C3259F7ADB87DC8061BE31613D99D9F473F44EBF5A6965FF39A839FCBC50952E` |

四库 final health 均 `auto_analysis_ready=true`、`hexrays_ready=true`；收尾 session/IDA process
审计为零。

## 11. 未扩张范围

本轮不假设具体 manager 的 Release 必然触发自注销，也不把 callback 后任意 UAF 结果规定成稳定
业务输出。window owner 在何时启动 root deletion、manager concrete destructor 对 draw buffer 的
进一步 teardown，以及失去 root 后 manager data 中悬空 item 的后续消费者，属于下一层 owner
lifecycle 审计。
