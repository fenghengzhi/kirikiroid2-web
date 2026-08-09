# Follow-up：`CreateAdaptor == null` 的真实 Android ARM64 运行时闭环

日期：`2026-08-02`。本文件补齐此前唯一不依赖损坏 PSB/MDF 物料、但尚未取得真实
Android 运行时覆盖的边界：`PSBMedia_EnsureContainer_guess@0x599E04` 在
`ncbInstanceAdaptor_PSBFile_CreateAdaptor_guess@0x59A330` 返回 null 时的状态提交、
所有权与同 container 重试行为。114 个 emitted 入口的静态六维判定不变；本轮增加的是
对既有 `ALIGNED` 结论的运行时验证。

## 权威制品与环境

- Android `libkrkr2.so`：SHA-256
  `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`；设备端
  `/data/local/tmp/libkrkr2.so` 再次计算得到同一值。
- 设备：Android API 31 ARM64 AVD，`ro.product.cpu.abi=arm64-v8a`。
- 输入：仓库已有 `tests/test_files/emote/ezsave.pimg`，SHA-256
  `d90d4ee955258b63efdc648f159990aa2c605dceef396ab9ea56eb8d281a7aa3`；没有改写其
  任何字节，也没有构造损坏 PSB/MDF。
- Full TJS 启动物料：已有
  `reference/xp3/caution_minimal/caution_minimal.xp3`，SHA-256
  `1206366927a05c3e17e8845debbf046c4dac7b46fe85f6a46d56407dad2ca490`。

## Fresh Android 证据

本轮 fresh 调用 IDA MCP `decompile(addr="0x599E04")` 与
`decompile(addr="0x59A330")`。关键伪代码为：

```text
file = new PSBFile; if !file.LoadStorage(container): delete file; return false
adaptor = CreateAdaptor(file, false, false)
next = Void
if adaptor != null: next = Object(adaptor, adaptor); adaptor.Release()
self._file = next
self._container = container
return true
CreateAdaptor: if ClassInfo.classObject == null and !throwOnFail: return null
```

`0x599F18` 把 `sticky=false, throwOnFail=false` 传给 `0x59A330`；null 返回走
`0x599FB8`，随后仍执行 `_file` CopyRef、container 赋值和 true 返回。该路径没有
`delete file`，因此成功加载的 raw `PSBFile` 没有被 adaptor 认领并泄漏。缓存命中又在
`0x599E60` 先要求 `_file.Type == Object`，所以恢复 class object 后，同 container 请求不能
被刚写入的 container 字符串单独命中，必须重新加载。

本地 `cpp/plugins/psbfile/PSBMedia.cpp:30-49` 逐行保持该结构：raw `new PSBFile`、失败时
显式 delete、无 owning RAII；`CreateAdaptor(file)` 为 null 时 `nextFile` 留为 Void；无论
是否得到 adaptor都依次赋 `_file`、赋 `_container` 并返回 true。生产实现本轮无需修改。

## Oracle 方法

新增 `run_psbfile_load_adb.py --media-adaptor-null`，实现位于
`tests/differential/oracle_runner/adapters/psbfile_load.py:521-641`：

1. 用现有 XP3 启动真实 Full TJS，并确认 PSBMedia singleton 与 PSBFile class object 都已
   注册；
2. 把现有 `ezsave.pimg` 以 ASCII container 名放入 app-private storage；
3. 只把目标内 `PSBFile_ncbClassInfo_classObject@0x1AB5110` 的一个指针槽暂时写成 0；
4. 直接调用原始 `EnsureContainer@0x599E04`，避免后续 `Resolve` 对 Void `_file` 的 null
   first-fault 干扰当前观察；
5. 读取原始 ARM64 `PSBMedia` 字段，核对返回值、Variant type、Object/ObjThis 和 container；
6. 在 `finally` 路径恢复完全相同的 class-object 指针，再用同一 name 调用一次
   `EnsureContainer`，验证 Object gate 迫使重新加载并发布 adaptor。

该方法没有伪造输入内容；唯一状态注入正是目标函数读取的 class-object null 条件，并在
任何正常或异常出口恢复。首次加载故意遵循原版泄漏边界；oracle 不尝试从丢失的 raw 指针
反向回收对象。

## 实测结果

命令：

```text
python3 tests/differential/python/run_psbfile_load_adb.py \
  --serial emulator-5554 \
  --media-adaptor-null \
  --startup-xp3 reference/xp3/caution_minimal/caution_minimal.xp3
```

关键结果：

```text
first_return=true
null_file_type=0
null_file_object=0
null_file_objthis=0
null_loaded_container="psb-media-adaptor-null.pimg"
class_slot_restored=true
retry_return=true
retry_file_type=1
retry_file_object==retry_file_objthis!=0
retry_loaded_container="psb-media-adaptor-null.pimg"
status="ok"
```

这同时观察到 null-adaptor 路径的 true 返回、Void `_file`、container 提交，以及恢复后的
同名重载。泄漏本身仍由 `0x599ED0` allocation 到 null 分支之间不存在 delete 的静态指令
链证明；运行时没有为了“测泄漏”改写 allocator 或恢复原版故意丢失的指针。

## 审计影响

- `CreateAdaptor-null` 不再属于运行时验证缺口。
- 尚缺天然运行时物料的项目收束为：损坏 MDF/zlib failure、filter 后 offset failure、
  损坏 packed table、自然 tag `0x0B` 极端值和 >4 GiB storage。
- 114 个 emitted 入口统计继续为
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`；本轮没有用 runtime PASS 升级任何
  stripped/O3 源码 token 的证据等级。
