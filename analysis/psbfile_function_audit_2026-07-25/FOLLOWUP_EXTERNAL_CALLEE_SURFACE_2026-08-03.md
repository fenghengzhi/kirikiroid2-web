# Follow-up：MANIFEST 外 direct callee semantic surface

日期：`2026-08-03`。本轮继续只读权威 Android ARM64 `libkrkr2.so`。在完整 callsite
surface 已固定 435 个外部 direct site 的基础上，本轮逐个恢复其 65 个唯一 target 的
语义角色，重点排除“某个 `sub_*` 实际是遗漏的 psbfile 私有 helper”。没有修改 `cpp/`
或测试物料。

## 结论

- 435 个 MANIFEST 外 `BL/B` site 精确指向 **65** 个唯一 target。
- 65 个 target 全部落入 9 类通用边界：EH/runtime、分配/内存/压缩、libstdc++
  string/vector、storage/stream、script global、诊断/log、ttstr/narrow、ncbind、
  Variant/closure。
- 32 个原先仍名为 `sub_*` 的 target 已 fresh decompile；没有一个实现 PSB classifier、
  packed table、raw node、media resolve 或 NCB PSB 专属业务逻辑。
- 外部调用层与本地 `PSBRawFile.cpp`、`PSBMedia.cpp`、`main.cpp`、`StorageIntf.h`、
  `tjs.h/tjsVariant.h`、`ncbind.hpp/ncb_invoke.hpp` 一致，没有新增生产 GAP。
- 32 个 stripped helper 已在 IDB 中按最强证据加 `_guess` 名和函数头注释；原始源码
  spelling 不可由行为唯一恢复，因此没有伪装成确定符号。

## 枚举方法

对 114 个 MANIFEST FDE 重新解码全部 `BL imm26`，并只纳入 destination 落在 owner FDE
外的 `B imm26`。去掉 destination 本身属于 MANIFEST 的 44 个内部 site 后，得到 435 个
外部 site。每个 target 记录精确 site 数；完整 owner/site/target 三元组仍由 callsite SHA
固定，本轮新增的是显式 target→语义角色清单。

## 65 个 target

### 1. runtime / EH：9 target，140 site

| target | site | 语义 |
| --- | ---: | --- |
| `4013C0` | 1 | `__cxa_guard_abort` |
| `406D70` | 31 | `__stack_chk_fail` |
| `408BD0` | 2 | `__cxa_begin_catch` |
| `4139B0` | 2 | `__cxa_rethrow` |
| `416120` | 1 | `__cxa_guard_acquire` |
| `41A950` | 28 | `_Unwind_Resume` |
| `422530` | 2 | `__cxa_end_catch` |
| `425270` | 1 | `__cxa_guard_release` |
| `520FAC` | 72 | catch 后 `std::terminate` 的 compiler landing helper |

这些边与 39 张 LSDA 的 cleanup/catch-all 拓扑一致。`520FAC` 不是业务异常处理；它只在
catch landing 中进入 `std::terminate`。

### 2. allocation / memory / compression：9 target，103 site

| target | site | 语义 |
| --- | ---: | --- |
| `414210` | 15 | `operator new` |
| `415740` | 44 | `operator delete`，含 deleting-destructor tail |
| `418940` | 1 | `memmove` |
| `41F240` | 1 | `memcpy` |
| `420200` | 2 | zlib `uncompress` |
| `426B40` | 3 | `operator delete[]` |
| `A0DE48` | 4 | `TJSAlignedAlloc(bytes, align_bits)`；`align_bits=4` 即 16-byte alignment |
| `A0DE90` | 24 | `TJSAlignedDealloc`；从 aligned pointer 的 `-8` 取回原 allocation |
| `14A3C0C` | 9 | `operator delete` thunk |

这组固定 MDF success/failure 分配族、raw owner 销毁以及 vector/string 清理。目标确实把
第二参数 4 解释为 alignment bit count；本地调用 `TJSAlignedAlloc(size, 4)` 保留同一 API，
没有把它误写成“四字节对齐”。

### 3. libstdc++ string / vector：6 target，8 site

| target | site | 语义 |
| --- | ---: | --- |
| `40CD20` | 1 | `vector<string>::reserve` |
| `423250` | 1 | `vector<string>::_M_emplace_back_aux` |
| `149DF58` | 1 | throw `std::bad_alloc` |
| `149E210` | 2 | throw `std::length_error` |
| `14A29F4` | 1 | `std::string::assign(const char*, size_t)` |
| `14A3D90` | 2 | COW `std::string` copy/retain-or-clone helper |

它们只服务 DecodeName/Dictionary key vector 与 NCB parameter-vector cleanup，不是目标私有
容器的替代实现。

### 4. storage / stream：5 target，5 site

| target | site | 语义 |
| --- | ---: | --- |
| `8EA2C8` | 1 | `TVPRegisterStorageMedia` wrapper |
| `8EB8A0` | 1 | `TVPGetPlacedPath` |
| `8ECD90` | 1 | `TVPCreateStream(name, flags)` |
| `8F7C74` | 1 | borrowed `tTVPMemoryStream` constructor |
| `98063C` | 1 | `tTJSBinaryStream::ReadBuffer`；虚调 `Read` 并要求 full count |

`PSBFile::LoadStorage` 先显式 `TVPGetPlacedPath(name)`，随后 `TVPCreateStream(..., READ)`
内部再执行 read-path placement；本地保留这两层，不能合并成裸平台文件读取。
`PSBMedia::Open` 构造的是 borrowed memory stream，仍不持有 raw owner。

### 5. script global：1 target，4 site

| target | site | 语义 |
| --- | ---: | --- |
| `8E3C20` | 4 | 读取并 AddRef 全局 script dispatch |

四处 caller 分别属于 adaptor creation、Unregist 与 RegistEnd，和 ncbind 的
`TVPGetScriptDispatch()` 调用层一致。

### 6. diagnostic / log：3 target，27 site

| target | site | 语义 |
| --- | ---: | --- |
| `95440C` | 22 | 单 UTF-16 message 构造并 throw `TJS::eTJSError` |
| `95458C` | 3 | 一个 ttstr 参数格式化后 throw `TJS::eTJSError` |
| `A183A4` | 2 | level-0 log，即 ncbind `TVPAddLog` 路径 |

所有 classifier/type/storage/adaptor 失败都落在现有本地
`TVPThrowExceptionMessage`/`NCB_WARN` 表达，没有被本地 C++ exception 类型替换。

### 7. ttstr / narrow string：12 target，35 site

| target | site | 语义 |
| --- | ---: | --- |
| `54DEFC` | 1 | `ttstr = const tjs_char*`，含 COW/null/empty 路径 |
| `9B1D4C` | 2 | `tTJSNarrowStringHolder` UTF-16→UTF-8 构造 |
| `9B1EAC` | 5 | narrow holder owned-buffer 析构 |
| `9B1ED0` | 3 | `wcscmp_utf16` |
| `A0BB90` | 2 | integer→`ttstr` 构造 |
| `A0CA58` | 4 | `ttstr::SubString`；full-span retain / partial-span allocate |
| `A0CBEC` | 1 | `ttstr::IndexOf` |
| `A13390` | 6 | `ttstr::c_str` |
| `A1359C` | 2 | 两个 UTF-16 buffer 拼接为新 backing |
| `A136C0` | 6 | null-terminated UTF-16→ttstr backing |
| `A1381C` | 1 | pointer+length UTF-16→ttstr backing |
| `A13878` | 2 | narrow UTF-8→ttstr backing |

这组证明 Resolve 的 segment copy/no-op lifetime、EnumMembers 的 narrow direct assignment、
media name assignment和 NCB diagnostic concat 均经过原 TJS string 层。

### 8. ncbind：6 target，14 site

| target | site | 语义 |
| --- | ---: | --- |
| `9F4F18` | 2 | native class name find-or-append / ID 返回 |
| `9F538C` | 2 | `TJSCreateNativeClassMethod` |
| `9F5858` | 1 | native class object/class-id/finalize 初始化 |
| `9F5AF4` | 3 | `TJSNativeClassRegisterNCM`/member registration |
| `9F6D2C` | 3 | typed wrapper 的 `ncbNativeClassMethodBase` 构造 |
| `9FBBA4` | 3 | 三只 typed wrapper 的 base 析构 |

因此 factory/root/load 不是三个手写 dispatch 对象；目标与本地都保留 ncbind base + typed
derived wrapper + member/factory callback 分层。

### 9. Variant / closure：14 target，99 site

| target | site | 语义 |
| --- | ---: | --- |
| `A0E0F4` | 1 | 分配 Variant Octet backing |
| `A0E48C` | 1 | Variant 类型转换错误构造/抛出 |
| `A0E9EC` | 2 | closure Object 为空时 `TJSThrowNullAccess` |
| `A0F5E0` | 4 | Variant copy constructor |
| `A0F778` | 32 | Variant destructor wrapper |
| `A0F790` | 5 | Variant `ReleaseContent` |
| `A0FB64` | 6 | Variant `CopyRef` |
| `A0FE2C` | 1 | Variant string assignment |
| `A0FEB4` | 2 | narrow UTF-8 string assignment |
| `A0FEF0` | 1 | bool→`tvtInteger` assignment |
| `A0FF28` | 2 | int32→`tvtInteger` assignment |
| `A0FF60` | 1 | int64→`tvtInteger` assignment |
| `A0FF94` | 1 | double→`tvtReal` assignment |
| `A13274` | 40 | shared TJS/ttstr/Variant backing release |

这一组完整覆盖 CreateVariant、EnumMembers、root/load typed wrapper、media cache与 NCB
registration 的临时对象生命周期。没有 `std::variant`、裸 union 赋值或跳过引用计数的本地
简化。

## IDB 改善

32 个原 `sub_*` 现均使用 `_guess` 名，例如：

```text
TVPCreateStream_guess@8ECD90
tTJSBinaryStream_ReadBuffer_guess@98063C
tTJSNarrowStringHolder_ctor/dtor_guess@9B1D4C/9B1EAC
TJSRegisterNativeClass_guess@9F4F18
ncbNativeClassMethodBase_ctor/dtor_guess@9F6D2C/9FBBA4
TJSAlignedAlloc/Dealloc_guess@A0DE48/A0DE90
tTJSVariant_assign_{narrow,bool,int32,int64,double}_guess@A0FEB4..A0FF94
```

每个函数头同时记录行为证据和“原 spelling 已被 stripped”的限制，IDB 已保存。

## 机械门禁

`verify_elf_surface.py` 新增 65 行 external target manifest，要求：

1. 从真实 `BL/B` 解码得到的 target 集合完全相等；
2. 每个 target 的 site 数精确一致；
3. 九类 target/site 汇总分别一致；
4. 总数保持 65 target / 435 site。

通过输出：

```text
external_callee_surface=true targets=65 sites=435 roles=9 classified=true
```

完整 callsite SHA 继续固定每个 owner/site/target；本清单补充 source-facing 语义，防止把
通用 runtime/NCB/TJS 边界误判成缺失的 psbfile 私有函数。
