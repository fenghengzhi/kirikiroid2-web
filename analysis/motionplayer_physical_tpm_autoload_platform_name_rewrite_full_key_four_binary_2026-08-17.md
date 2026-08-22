# 物理 .tpm autoload、iOS discovery rewrite 与完整 Path/Name module key 四端恢复

日期：2026-08-17

## 1. 范围与结论

V211 恢复了 script-visible `Plugins.link` 的 exact-key 行为，但当时尚未逐指令检查
`tvpLoadPlugins` 的物理 `.tpm` autoload。本轮只以四个当前参考二进制为事实源，闭合：

- 三个扫描目录与 regular-file/extension filter；
- `tTVPFoundPlugin {Path, Name}` 容器与排序键；
- Android/iOS 对 discovered Name 的不同 materialization；
- autoload count、日志、Path/Name join 与 loader call；
- loader bool、异常、短文件名与同名排序边界。

四端共同与分歧结论：

1. 四端都先执行 V210 的 `TVPLoadInternalPlugins`，再扫描 project directory、
   `project/system`、`project/plugin`；
2. callback 只接受 `mask & S_IFREG` 且最后四字节与 `.tpm` case-insensitive 相等的 entry；
3. callback 在 `filename.length()-4` 之前没有 length guard，长度小于 4 的 regular filename
   保留原实现的越界 pointer arithmetic 边界；
4. **Android arm64/armv7** 把 `Name=原始 filename`，因此保留 `.tpm`；
5. **iOS arm64/armv7** 在 discovery callback 中构造
   `Name=filename.substr(0,len-4)+".dll"`；rewrite 发生在入列前；
6. 四端都按 `Name` 的 `std::string::operator<` 排序，并在加载 loop 前把完整 vector size 写入
   `TVPAutoLoadPluginCount`；
7. 每项先记录 `"(info) Loading " + Name`，再构造 `Path + "/" + Name`，把**完整 joined
   path** 转为 `ttstr`，直接调用 public NCB `LoadModule` 并忽略 bool；
8. 四端加载阶段均不调用 `TVPExtractStorageName`，也不做第二次 `.tpm -> .dll` rewrite；
9. 因此当前端口统一执行“rewrite 后取 basename”的 `TVPLoadInternalPlugin` 与四端都不符；
   源码已删除该 helper，并用 `__APPLE__` 表达 discovery-time 平台分支；
10. Web/Wasmtime 不是 Apple target，因而采用 Android/非 Apple 分支：保留 discovered `.tpm`
    Name，并仍把完整 Path/Name key 交给 integrated loader。

## 2. 四端映射

| 目标 | `tvpLoadPlugins` | discovery callback | final LoadModule call | discovery Name |
|---|---:|---:|---:|---|
| Android arm64 | `0x907618` | `0x9089B0` | `0x907A00` | 原始 `.tpm` |
| Android armv7 | `0x6C77BC` | `0x6C8088` | `0x6C7906` | 原始 `.tpm` |
| iOS arm64 | `0x1003F1CD4` | `0x1003F2538` | `0x1003F1EAC` | 替换为 `.dll` |
| iOS armv7 | `0x3D923C` | `0x3D9C60` | `0x3D943E` | 替换为 `.dll` |

恢复名：

- `tvpLoadPlugins_guess`；
- `TVPSearchPluginsAt_callback_guess`。

Android arm64 的 callback 被 IDA 错误并入 V211 getList 后面的相邻大函数；本轮像 V212 一样
使用 code-label rename/comment/bookmark，不破坏性 undefine 大范围代码。

## 3. 扫描目录与 found-record 容器

四端共同源级骨架：

```cpp
void tvpLoadPlugins_guess() {
    TVPLoadInternalPlugins();

    std::vector<tTVPFoundPlugin> list;
    std::string project = ExtractFileDir(TVPNativeProjectDir);
    TVPSearchPluginsAt(list, project);
    TVPSearchPluginsAt(list, project + "/system");
    TVPSearchPluginsAt(list, project + "/plugin");
    std::sort(list.begin(), list.end());
    TVPAutoLoadPluginCount = static_cast<tjs_int>(list.size());

    for(const auto &item : list) {
        TVPAddImportantLog(L"(info) Loading " + ttstr(item.Name));
        ttstr key((item.Path + "/" + item.Name).c_str());
        (void)ncbAutoRegister::LoadModule(key);
    }
}
```

found record 是两个相邻 `std::string`：

| ABI | `std::string` source representation | record stride |
|---|---|---:|
| Android arm64 libstdc++ COW | one 8-byte representation pointer each | `0x10` |
| Android armv7 libstdc++ COW | one 4-byte representation pointer each | `0x08` |
| iOS arm64 libc++ SSO | 24 B each | `0x30` |
| iOS armv7 libc++ SSO | 12 B each | `0x18` |

排序 comparator 只读取第二个 string `Name`。Path 不参与 tie break。同一 Name 来自三个目录时
属于 equivalent elements；`std::sort` 不是 stable sort，不能为相同 Name 推断固定目录优先级。

## 4. discovery callback

### 4.1 共同 filter

```cpp
if(mask & S_IFREG) {
    if(strcasecmp(filename.c_str() + filename.length() - 4,
                  ".tpm") == 0) {
        // platform-specific record materialization
    }
}
```

四端把 POSIX `S_IFREG` 的 bit 15 当 gate：Android armv7 反编译表现为 signed 16-bit
`mask < 0`，其它端为 test bit `0x8000`，语义相同。

边界：

- suffix 只比较最后四个 narrow bytes；接受 `.tpm`、`.TPM` 等；
- 不要求之前存在 basename 或其它 dot；
- 目录 entry 不入列；
- filename 长度小于 4 时，`end-4` 在 buffer 前，属于原版没有 guard 的 malformed filesystem
  边界；当前源码保留该次序，没有“顺手修安全”。

### 4.2 Android record materialization

```cpp
tTVPFoundPlugin item;
item.Path = folder;
item.Name = filename; // .tpm retained
list.emplace_back(item);
```

两个 Android callback 内都只有 `.tpm` literal，没有 `.dll` literal、substring 或 append call。
加载日志显示原 `.tpm` Name，joined key 也以 `.tpm` 结尾。

### 4.3 iOS record materialization

```cpp
tTVPFoundPlugin item;
item.Path = folder;
item.Name = filename.substr(0, filename.length() - 4) + ".dll";
list.emplace_back(item);
```

两端 iOS callback 同时引用 `.tpm` 与 `.dll`。substring/append 在 vector emplacement 前完成；
因此后续排序、autoload count 对 record 数量不受影响，但排序键、日志和 joined key 都已是
`.dll` 名。

callback 构造 Path、substring、rewritten Name 或 vector growth 抛异常时，iOS armv7 SjLj
landing 按 call-site state 释放 partial libc++ strings/record 后传播；没有把 allocation failure
转为“跳过该文件”。

## 5. sort、count、log 与 load commit 时序

共同顺序严格为：

```text
完成三个目录扫描
  -> sort whole vector by Name
  -> TVPAutoLoadPluginCount = whole vector size
  -> for each sorted record:
       log Name
       join Path + "/" + Name
       convert joined narrow path to ttstr
       public LoadModule(joined key), ignore bool
```

所以：

- count 表示 discovered record 数，不表示成功加载数；
- 第一个 loader callback 抛异常前，count 已经发布为完整 discovered size；
- 日志发生在 load 之前；missing/already-loaded 的 `false` 仍留下 Loading 日志；
- bool 不会减少 count，也不会生成 Success/Failed suffix；
- loader callback 异常继续传播，function 的 string/vector EH 清理已构造状态；
- `Path + "/" + Name` 无条件插入 `/`；Path 为空时 key 仍以 `/` 开头；
- public loader 只 lowercases 整个 joined key，不提取 basename。

integrated NCB map 通常以 registrar 声明的 module name 为 key。因 autoload 使用完整 path，
同 basename registrar 不会仅因物理 `.tpm` 被发现而自动命中；这正是四端的可观察调用边界，
不能用端口 helper 悄悄变成 basename lookup。

## 6. 当前源码纠偏

`cpp/core/plugin/PluginImpl.cpp` 完成：

1. 删除 `TVPLoadInternalPlugin` forward declaration 与整个手写 helper；
2. `TVPLoadPlugin(name)` 改为 direct public `ncbAutoRegister::LoadModule(name)`，ignore bool；
3. `TVPSearchPluginsAt` 在共同 `.tpm` filter 后保留 `{Path, original Name}`；
4. `#ifdef __APPLE__` 下把 Name materialize 为
   `substr(0,len-4)+".dll"`，精确放在发现/入列阶段；
5. loop 继续使用现有 `Path + "/" + Name`，不添加 basename extraction。

这也纠正 V211 报告中“autoload 仍通过 storage-name/.dll normalizer”的阶段性假设；V211 对
script-visible `Plugins.link` 的 direct-key 结论不变。

## 7. 回归测试

测试 TU 新增 `autoload-path-probe.dll` registrar 和计数器。回归显式调用产品
`TVPLoadPlugin`：

```text
plugin/autoload-path-probe.tpm -> complete-key map miss
plugin/autoload-path-probe.dll -> complete-key map miss
```

两次都要求 callback count 保持 0、basename registered marker 不出现。第一项击中旧 helper 的
“rewrite + extract basename”分支；第二项单独证明即使 suffix 已是 `.dll`，也不能在 load 阶段
提取 basename。

Web/Wasmtime 走非 Apple source branch。iOS discovery rewrite 由两份 iOS binary 的 substring/
`.dll` append 指令链和源码条件分支记录；当前 test target 不伪装 `__APPLE__` 运行环境。

## 8. recovery IDB 写回

四份 recovery IDB 已顺序打开、写回、保存、关闭，最终无 session：

- 8 项 semantic rename；A64 callback 为 code-label，其余为 function；
- 4 项 `tvpLoadPlugins` function type application；
- 7 条 function set comment；
- 11 条 suffix/rewrite/final-loader line comment；
- 8 个 bookmark。

四端 comment 分别明确 Android retain/iOS rewrite，防止以后再次把单一平台行为外推为共同
helper。

## 9. 验证与产物差异

- ordinary/headless test TU syntax-only：通过；
- 仅有既有 `_tss` literal-operator warning；
- Web Debug 与 Wasmtime Headless Debug 完整构建：通过；
- 两个 Wasm 均 `WebAssembly.validate=true`；
- imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- 两个 CTest tree exit 0，明确报告 `No tests were found`；新增 probe 由两种 test TU
  syntax-only 完整编译，未虚报 runtime execution；
- `git diff --check` exit 0，仅有工作树既有 LF→CRLF warning。

产品产物：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,793 | `858A3677901252A11D37637BC3BE7423D1ACD9D019080E64E18276379CE49D55` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,934 | `FC8847E666976A424C9BD1A4780E5124F071D114CB6373B1F6985AC350A22C08` |

相对 V212：Web 减少 1,387 B，Wasmtime 减少 1,419 B：

| section | Web V213 | Web delta | Wasmtime V213 | Wasmtime delta |
|---|---:|---:|---:|---:|
| FUNCTION | `0x1BD2D` | `-2` | `0x1BA4C` | `-2` |
| GLOBAL | `0xD5C2` | 0 | `0xD5EA` | 0 |
| CODE | `0x1A41AB5` | `-0x509` | `0x19E9A63` | `-0x509` |
| DATA | `0x5A3F00` | 0 | `0x5A1150` | `-0x20` |
| name | `0x3185E4E` | `-0x60` | `0x3141CE4` | `-0x60` |

两端 CODE/FUNCTION/name delta 同构；Wasmtime 另少 0x20 DATA。ABI import/export/global surface
不变。差异符合删除统一 normalizer/helper 及其 literals/callees，而 Web target 没有编译 Apple
rewrite branch。

## 10. 未过度推断的部分

- Android/iOS 分支用 `__APPLE__` 表达，是与四个 target family 完全吻合的最小源码结构；
  stripped binary 不能证明原作者使用的预处理 symbol 拼写；
- 本轮只恢复 integrated internal loader 的 autoload path；它没有动态 `dlopen` physical plugin
  路径，不能把历史桌面 KiriKiri plugin loader 行为外推到这四端；
- equal Name 的跨目录排序顺序依具体 `std::sort` 实现/输入排列，不声明稳定目录优先级；
- 短 filename 的 end-4 边界已记录但未“安全修复”，因为四端都没有 guard；
- iOS branch 没有由当前 Web test runtime 执行，但两份独立 iOS binary 已给出同构 rewrite/EH；
- 本纵切面不代表 motionplayer 总目标完成。
