# NCB 重复 AllRegist、append-only borrowed index 与静态 teardown 四端恢复

日期：2026-08-17

## 1. 范围与结论

V214 确认 `tvpLoadPlugins` 可以重复进入且入口不重置 autoload count。本轮继续沿重复启动路径，
审计 `ncbAutoRegister::AllRegist` 是否幂等、内部 pointer-list 如何累积、异常前缀如何影响后续
motionplayer/emoteplayer 注册，以及 global set/map 在进程退出时实际做什么。

事实源仍严格限定为 `reference/binaries/` 四个当前参考二进制。四端共同结论：

1. `AllRegist(line)` 不是 once-only initializer；每次调用都从该 line 的 registrar head 重新遍历
   完整链；
2. 每遇到一个 registrar 都重新分配一个 `std::list` node，把同一个静态 registrar pointer
   `push_back`；没有 clear、find/dedupe、generation 或 already-indexed gate；
3. pointer 是 borrowed：list node 拥有的只有 pointer value，不拥有/删除 registrar 对象；
4. 完整重复 `K` 次后，每个 module/line 的 callback occurrence 也扩大为 `K` 倍；loader 按
   line-major 顺序执行，而不是按 indexing generation 交错执行；
5. `TVPRegisteredPlugins` guard 发生在 callback loop 之前：已 committed module 的重复节点保持
   dormant；尚未 committed 或上次失败的 module 会执行全部重复 occurrence；
6. AllRegist 自身也非事务。ttstr/map/list 任一步抛异常时，已经 append 的旧 prefix 不回滚；
   下一次调用又从 head 开始，使 prefix 比 suffix 多一代；
7. global initializer 先构造 registered set，再构造 internal map，并依次注册 atexit destructor；
   退出时逆序为 internal map 先析构、registered set 后析构；
8. 两个容器 destructor 只释放 ttstr/tree/list nodes，不遍历 registrar vtable、不调用
   `Unregist`、不撤销 script/native class publication；
9. 当前源码的 append-only 行为已经与四端一致。本轮不添加一次性 guard/dedupe“修复”，只把
   这个容易被误改的边界精确写入注释与 recovery IDB。

## 2. 四端映射

| 目标 | `AllRegist(line)` | registrar heads | registered set | internal map | set/map init | set dtor | map dtor |
|---|---:|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x548EAC` | `0x1AB5920` | `0x1AB5938` | `0x1AB5968` | `0x42F408` | `0x701DB0` | `0x701DCC` |
| Android armv7 | `0x4A96A0` | `0x1111BC0` | `0x1111BCC` | `0x1111BE4` | `0x3018E0` | `0x5BA8E0` | `0x5BA8E4` |
| iOS arm64 | `0x100287B8C` | `0x10256B8F8` | `0x10256B910` | `0x10256B928` | `0x1002A03DC` | `0x10029FD94` | `0x10029FDBC` |
| iOS armv7 | `0x28A950` | `0x218F184` | `0x218F190` | `0x218F19C` | `0x2A4DD8` | `0x2A48DC` | `0x2A48EC` |

recovery IDB 使用：

- `ncbAutoRegister_AllRegistLine_guess`；
- `ncbAutoRegister_top_guess`；
- `TVPRegisteredPlugins_guess`；
- `ncbAutoRegister_internal_plugins_guess`；
- `ncbPluginGlobalContainers_Init_guess`；
- `TVPRegisteredPlugins_Dtor_guess`；
- `ncbInternalPlugins_Dtor_guess`。

stripped/private 名继续保留 `_guess`。

## 3. registrar head-chain 形状

四端 `AllRegist` 对 registrar base 的读取一致：

| ABI | vptr | module-name | next | base footprint |
|---|---:|---:|---:|---:|
| LP64 | `+0x00` | `+0x08` | `+0x10` | `0x18` |
| ILP32 | `+0x00` | `+0x04` | `+0x08` | `0x0C` |

`top[3]` 分别是 PreRegist、ClassRegist、PostRegist 的 head。每个静态 registrar constructor 都
执行 head insertion：

```cpp
next = top[line];
top[line] = this;
```

所以每次 AllRegist 遍历得到相同 head-chain order，也就是该 line 静态构造顺序的反序。重复
调用不改变 `_next`，只在下游 list 再存一份相同 pointer 序列。

## 4. append-only list ABI

共同伪代码为：

```cpp
void ncbAutoRegister_AllRegistLine_guess(unsigned line) {
    for(const ncbAutoRegister *p = top[line]; p; p = p->next) {
        ttstr key(p->moduleName);
        key.ToLowerCase();
        internalPlugins[key].lists[line].push_back(p);
    }
}
```

四端 list 形状：

| 目标族 | list object stride | list node size | node payload | explicit size field |
|---|---:|---:|---:|---|
| Android LP64 libstdc++ | `0x10` | `0x18` | `+0x10` | 此旧 ABI 路径不维护 |
| Android ILP32 libstdc++ | `0x08` | `0x0C` | `+0x08` | 此旧 ABI 路径不维护 |
| iOS LP64 libc++ | `0x18` | `0x18` | `+0x10` | 每次 insert 后 `++size` |
| iOS ILP32 libc++ | `0x0C` | `0x0C` | `+0x08` | 每次 insert 后 `++size` |

因此 `INTERNAL_PLUGIN_LISTS { list[3]; }` 的 payload 为 Android LP64/ILP32 `0x30/0x18`，
iOS LP64/ILP32 `0x48/0x24`。这里不能把 iOS list 的 size field 反推到 Android 的旧 libstdc++
layout。

四端都能在循环体看到每 occurrence 一次 allocator call 和 payload pointer store。没有 conditional
branch 比较旧 tail payload，也没有 set/hash side container；“重复 pointer 会被 std::list 自动
去重”不成立。

## 5. 重复调用的精确执行顺序

设一个 module 每行原始链块分别为：

```text
Pre   = [P0, P1]
Class = [C0, C1]
Post  = [Q0]
```

连续两次完整 `AllRegist()` 后，三个 list 是：

```text
Pre   = [P0, P1, P0, P1]
Class = [C0, C1, C0, C1]
Post  = [Q0, Q0]
```

inner loader 固定按 line 0→1→2 遍历，所以首次未 committed load 的 callback 顺序是：

```text
P0 P1 P0 P1 -> C0 C1 C0 C1 -> Q0 Q0
```

它不是：

```text
(P0 P1 -> C0 C1 -> Q0) x 2
```

这个 line-major 差别对依赖 callback 很重要：全部重复 PreRegist occurrence 都在任何 ClassRegist
前运行。第一次 Pre dependency call 成功提交依赖后，同一 list 的后续重复 dependency call 会因
依赖 marker 已存在而返回 false；外层 callback 忽略该 bool 后继续。

## 6. 与 registered marker 的交互

### 6.1 已 committed module

`LoadModule_impl` 首先查 `TVPRegisteredPlugins`。命中时立即返回 false，不查 internal map，也不
遍历重复 list。因此：

- 第二次 startup 会继续为 xp3filter 累积 pointer node；
- 但本次 startup 紧随其后的 xp3filter load 被 marker guard 截断；
- 已加载 motion/emote/DrawDevice 的重复索引同样 dormant；
- dormant node 不会被 erase，持续占用至 process teardown。

### 6.2 indexed 但未 committed module

如果第二次 `tvpLoadPlugins` 发生在游戏尚未 `Plugins.link("motionplayer.dll")` 之前，motionplayer
三行 list 已各含两代相同 pointer。后续首次 link 会执行两代 callback occurrence，只有整个
expanded pipeline 正常返回后才写一个 module marker。

对依赖链：

```text
DrawDeviceD3DZ -> DrawDeviceD3D -> emoteplayer -> motionplayer
```

最内层尚未 committed module 会先执行其全部重复 list；成功后 marker 阻止同一外层 Pre block
中后续 dependency call 再执行依赖，但外层自己的 Class/Post duplicate 仍照常运行。具体 class
callback 重复发布造成覆盖、引用增长或脚本错误，取决于 callback 自身边界；本报告只断言已由
loader control flow 证明的“每 occurrence 均调用”。

## 7. 索引异常与不均匀 generations

AllRegist 不设置 rollback guard。假设某 line 的 head chain 是 `[A,B,C]`：

1. 第一次在处理 B 时抛异常；A 的 node 已留在 list；
2. 重试从 head 再开始；A 再 append 一次，随后 B/C 各 append 一次；
3. 最终 occurrence 是 `[A,A,B,C]`，不是完整两代，也不是一代。

异常可发生在 module-name ttstr 构造/lowercase、map key lookup/insertion 或 list-node allocation。
可证的共同边界是 completed prefix 不回滚。若 map node 已成功插入而后续 list allocation 失败，
该 lowercase module key/三个空或部分 list 也保持已索引状态；当前端口 source/test-only inline
`HasModule` 会报告 map hit，但它与“该行已有 callback”仍不能合并成同一事实。V216 已确认
四份最终参考镜像没有保留该 helper/function。

外层 `AllRegist()` 依次调用 line 0、1、2：

- line 0 失败时 line 1/2 本轮尚未开始；
- line 1 失败前，line 0 已完整增加一代；
- line 2 失败前，line 0/1 已完整增加一代；
- 下一次总调用仍从 line 0 开始，因而三行 generation 数可永久不相等。

如果随后 expanded callback pipeline 又抛异常，module marker 仍不提交；没有新一次 AllRegist
也会在下一次 LoadModule retry 从 list 头重跑全部 occurrence。若重试前又发生 AllRegist，重复
倍数还会继续扩大。

## 8. static container 初始化与退出析构

四端 global initializer 都按相同源级顺序：

```text
construct TVPRegisteredPlugins
register set destructor with __cxa_atexit
construct ncbAutoRegister::_internal_plugins
register map destructor with __cxa_atexit
```

atexit 逆序执行，所以 map 先、set 后：

```text
internal map dtor
  -> destroy lowercase ttstr keys
  -> destroy three list sentinels/nodes per map value
  -> free tree nodes
registered set dtor
  -> destroy committed ttstr keys
  -> free tree nodes
```

map list payload 只是 raw registrar pointer。四端 destructor 都不读取 payload vptr、不调用
`Regist/Unregist`、不 delete pointee。set destructor 同样只是 ordered-set teardown。由此可见：

- process exit 不是 plugin unload；
- script/native class property 不由这两个容器撤销；
- `AllUnregist` 的源码存在不等于它被 static container destructor 调用；
- map-before-set 顺序只保证两个 index/marker container 的节点寿命，不提供 module rollback。

V218 已继续闭合此前保留的 registrar teardown 问题：registrar 只含 borrowed literal/function
pointer，四端均没有为 registrar 登记 `__cxa_atexit` destructor，也没有 unlink/null top-chain 的
退出阶段。因此这里实际不存在 registrar 跨 translation-unit destructor 排序或 pointee 提前析构
造成的悬挂窗口；internal map teardown 仍只丢弃 raw pointer value。详见
`analysis/motionplayer_ncb_registrar_pointer_only_trivial_destructor_permanent_top_chain_four_binary_2026-08-17.md`。

## 9. 当前源码与测试策略

算法无需行为改动；`cpp/core/plugin/ncbind.hpp` 本已逐项 `push_back`。本轮补充：

- AllRegist 是 append-only、非幂等 index build；
- repeated startup 会放大尚未 committed module 的 callback occurrence；
- 索引异常只保留已完成 prefix；
- `cpp/core/plugin/ncbind.cpp` 的 set/map 构造/逆序析构与 borrowed ownership；
- `PluginImpl.cpp` 明确 startup 没有 once guard；
- unit fixture 的本地 `indexed` guard 仅避免测试进程污染，不代表产品/reference 有 guard。

没有在单元测试里主动调用第二次全局 `AllRegist`：该操作会永久复制所有尚未注册 module 的
process-global list，污染同一 Catch2 进程中后续 case，且参考实现没有 reset/erase API。重复
行为由四端每-occurrence allocation/store、无 dedupe branch、loader list traversal及完整 global
xref 共同闭合；不为测试引入 port-only container inspection/reset seam。

## 10. recovery IDB 写回

四库累计写回并保存关闭：

- 28 项 semantic rename：每端 4 个 function + 3 个 global；
- 16 项 function type application；
- 28 项 AllRegist/node/top/map/init/dtor comment；
- 16 项 append/allocation/init/dtor bookmark。

四库均已保存，最终 `idb_list` 为零 session。

## 11. 验证与产物

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 均通过；
- Web 82 个受影响 build step 全量重编译并成功链接；
- Wasmtime 119 个受影响 build step 全量重编译并成功链接；
- 两个 build tree 的 CTest 均返回 0，仍无已注册 CTest；
- 两份 Wasm 均 `WebAssembly.validate == true`；
- imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- `git diff --check` 返回 0，仅有仓库既有 LF/CRLF 提示；
- 编译告警仍为既有 `_tss`、PSD format/switch/deprecation 等，没有本轮新增错误。

产品产物与 V214 字节级一致，因为本轮只有注释：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85,657,793 B | `858A3677901252A11D37637BC3BE7423D1ACD9D019080E64E18276379CE49D55` |
| Wasmtime `index.wasm` | 85,004,934 B | `FC8847E666976A424C9BD1A4780E5124F071D114CB6373B1F6985AC350A22C08` |

section 同样不变：

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AB5` | `0x19E9A63` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 12. 未过度推断的部分

- 本文证明 callback occurrence 会重复调用；每个 callback 的二次发布后果仍由各自已恢复或待
  恢复的对象生命周期决定，不在这里统一假设为“安全”或“崩溃”；
- global container destructor 不调用 Unregist 是直接控制流证据；V218 又证明 registrar 自身
  没有 destructor registration，故不存在需要排序的 registrar teardown；
- 本纵切面闭合重复 NCB indexing 与 container teardown，不代表 motionplayer 总目标完成。
