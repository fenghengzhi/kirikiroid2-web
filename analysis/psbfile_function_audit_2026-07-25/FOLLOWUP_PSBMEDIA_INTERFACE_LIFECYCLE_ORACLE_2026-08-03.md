# Follow-up：PSBMedia vtable、引用计数与析构生命周期 oracle

日期：`2026-08-03`。本轮只使用 Android arm64 `libkrkr2.so`，为此前仅由静态审计覆盖的
PSBMedia 简单接口补 direct ABI/oracle：完整 11-slot vtable、非原子 AddRef/Release、固定
media 名、两个 Normalize no-op、GetLocallyAccessibleName 清空语义，以及 complete/deleting
两种析构边界。本轮没有修改 `cpp/`，没有生成或改写 PSB/MDF fixture，也没有生成 APK 或
Android 二进制。

fresh Android 证据与当前生产实现一致，没有发现新的确定 GAP；114 项审计仍保持
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。其中 `0x599DD8` 的精确空字符串源码
token 继续受 stripped/O3 限制；新增正常/空值运行时观察不能把它冒充为唯一恢复。

## fresh Android arm64 证据

本轮重新反编译并读取完整反汇编：

- `PSBMedia_completeDestructor_guess @ 0x5997F0`
- `PSBMedia_deletingDestructor_guess @ 0x599830`
- `PSBMedia_AddRef_guess @ 0x599878`
- `PSBMedia_Release_guess @ 0x599888`
- `PSBMedia_GetName_guess @ 0x5998A8`
- `nullsub_262 @ 0x5998BC`、`nullsub_263 @ 0x5998C0`
- `PSBMedia_GetLocallyAccessibleName_guess @ 0x599DD8`
- 构造/注册上下文 `PSBFile_preRegister_guess @ 0x59849C` 与 media record 构造
  `sub_8E8DA8 @ 0x8E8DA8`

不超过 10 行的关键伪代码：

```text
AddRef(self): self.ref32++
Release(self): if self.ref32==1: tail self.vptr[1](self); else self.ref32--
GetName(self,out): ttstr_assign(out, UTF16("psb"))
NormalizeDomain(self,name): return
NormalizePath(self,name): return
GetLocal(self,name): if name.storage!=null: Release(name.storage); name.storage=null
complete_dtor(self): restore media vptr; release container if nonnull; destroy file Variant
deleting_dtor(self): inline the same member teardown; operator delete(self)
```

关键机器码边界：

- `0x599878` 只有普通 `LDR W8 → ADD #1 → STR W8`，没有原子指令、锁、null 或饱和
  分支。
- `0x599888` 先做 32-bit `SUBS ref,1`；旧值恰为 1 时不写回而尾跳 vslot 1，所有其他
  值都写回减一结果。因此旧值 0 的可见结果是 `0xFFFFFFFF`，对象不删除。
- `0x5998A8` 把 X1 调整为输出 `ttstr*`，以原始 UTF-16LE `70 00 73 00 62 00 00 00`
  尾入 `sub_54DEFC`；不读取 self。
- `0x5998BC/0x5998C0` 各只有一条 `RET`，完整输入对象位不变。
- `0x599DD8` 对非空 storage 恰好一次 Release 后写 null；空 storage 直接返回；self
  未读取。
- 两种析构都先把 vptr 恢复为 `0x1A0B510`，再按 `container ttstr → _file Variant`
  顺序清理；complete 入口返回，deleting 入口继续尾入 `operator delete`。
- `0x59849C` 以 target `operator new(0x28)` 构造 `{vptr,ref=1,empty file,empty
  container}`；`sub_8E8DA8@0x8E8DFC` 经 vslot 2 再持有一次，所以注册完成后的进程期
  singleton 基线 refcount 为 2。

Android vtable address point `0x1A0B510` 的 11 项顺序为：

```text
5997F0 599830 599878 599888 5998A8 5998BC 5998C0
5998C4 59993C 5999F4 599DD8
```

## 本地生产实现逐项对照

当前 `cpp/plugins/psbfile/PSBMedia.h:12-25` 与 `PSBMedia.cpp:11-17,221-224`：

1. 构造器把普通 `int _ref` 设为 1；`AddRef` 仅执行 `_ref++`。
2. `Release` 只在 `_ref == 1` 时 `delete this`，否则 `_ref--`；没有安全化为
   `<=1`、atomic 或 clamp。
3. `GetName` 只做 `name = TJS_W("psb")`，与 target UTF-16 常量和赋值调用链一致。
4. `NormalizeDomainName`、`NormalizePathName` 均保留独立空 override。
5. `GetLocallyAccessibleName` 调用 `name.Clear()`；`tjsString.h:309-312` 正是
   `if(Ptr) Ptr->Release(), Ptr=nullptr`。
6. 默认虚析构让 C++ 依声明逆序清理 `_container` 后 `_file`，complete/deleting 两种
   ABI 入口由编译器生成；源码没有手写释放或改成智能指针。

数据流、虚调用链、对象生命周期、无容器边界和可观察错误边界均与 fresh Android 证据
一致。因此本轮不修改生产代码。

## direct oracle 设计

新增 `--media-interface`，在 Full TJS 已启动且 PSBFile 插件已注册后执行：

1. 从进程期 singleton 读取 address point 和全部 11 个 qword，逐项要求等于上述精确
   target 地址。
2. 经 vslot 2/3 调用 singleton，要求 refcount 精确 `2 → 3 → 2`，并在全部探针结束后
   仍为 2；不会触发 singleton 删除。
3. 用 target `ttstr` 构造器创建 `old-media`，经 vslot 4 覆盖为 `psb`，再由 oracle
   对当前 live storage 负责一次最终 Release。
4. 对 `MiXeD/Path` 依次调用 vslot 5/6，要求 storage 指针和读取的 64 个对象字节逐位
   不变，文本也保持原样。
5. 对 `private/path` 调 vslot 10，要求槽清零；再次以空槽调用仍为零，覆盖非空和空两条
   分支。
6. 使用 target 自身 `operator new(0x28)` 分配一只与构造器相同的空对象，仅把 refcount
   设为 0；vslot 3 后要求 `0xFFFFFFFF`、vptr 不变，再调用 complete destructor 与匹配
   target delete。该对象隔离了 underflow 边界，不破坏 singleton。
7. 第二只 target 分配的空对象保持 refcount 1；vslot 3 必须经 deleting destructor 完成
   terminal delete。RPC 一旦发起就先放弃 host 侧重试所有权，避免 lost reply 后 double
   delete。

手工对象只复用 target 构造器已经证明的空对象位布局，且由 target allocator 家族完整
分配/释放；它不是 PSB/MDF fixture，也不进入生产源码。Android 的 `0x28` 与字段偏移只
存在于 oracle adapter，不硬编码进 wasm C++。

## trace 与当前验证

`PSBFILE_MEDIA_TARGETS` 从 15 个扩为 23 个唯一地址，新增上述 8 个 canonical media
入口；每个地址都有完整 argument count、return kind 和稳定名称。通用
`ttstr_assign/Release`、Variant destructor 与 operator new/delete 没有加入 Frida 目标，
避免 Full TJS 初始化和字符串清理把 trace 淹没；它们的效果由 live 对象位和 target
入口返回直接约束。

当前已完成：

- 三个 Python 文件 `py_compile` 通过；runner `--help` 已公开 `--media-interface`。
- media trace catalog 为 `23/23` unique，names/args/returns 无缺项。
- 纯主机 fake control-flow 以同一 vtable/内存语义完整执行新 adapter，得到
  `status=ok`、`2→3→2`、zero-ref `0xFFFFFFFF` 和 terminal return；这只验证 adapter
  控制流，不冒充 Android runtime。
- `git diff --check` 通过。
- 本轮没有改变 harness protocol 或 native harness，现有 APK 已具备所需普通 `CALL`、
  `READ/WRITE`；当前三个可见 harness APK 的 native entries 全部只在
  `lib/arm64-v8a/`。legacy NDK 当前不可用，但本轮无需重建 native harness。

后续真实 Android ARM64 target 已执行以下模式，并在无 trace 与单次全量 trace 中均为
`ok`：

```bash
python3 tests/differential/python/run_psbfile_load_adb.py \
  --media-interface \
  --startup-xp3 reference/xp3/caution_minimal/caution_minimal.xp3 \
  --trace
```

固定 APK/目标哈希和事件数见
[FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。
该设备验证补充直接观察面，但不会消除 `0x599DD8` 的精确空字符串源码 token
`EVIDENCE_LIMITED`，也不会改变 114 项统计。
