# Follow-up：114 入口外部 caller 普查与 `ObjSource` 参数生命周期

日期：`2026-08-02`。本文件补足 114-address MANIFEST 的 caller 方向检查：逐个查询
Android `libkrkr2.so` 中每个入口的 direct code xref，先建立 MANIFEST 外 consumer
全集，再对此前未单独归档的 `Motion.ObjSource` width/height/clip/drawLayer 薄封装作
fresh 反编译。本轮唯一权威来源仍是 Android kirikiroid2 1.4.4
`reference/libkrkr2/libkrkr2.so`；这些 consumer 不进入 114-address 统计分母。

## 机械普查口径（2026-08-04 纠正）

原轮对 [MANIFEST.md](MANIFEST.md) 的 114 个唯一地址分十个一批执行
`xref_query(direction="to", xref_type="code", include_fn=true, count=5000)`，再按 caller
函数入口是否落在连续主实现簇 `0x59641C..0x59B9C8` 之外分类。2026-08-04 以权威 ELF
完整 `.text` 的 AArch64 immediate `B/BL` 扫描和 `.eh_frame` owner 归属独立复核后，发现
IDA 把两只本地 weak `std::vector<std::string>` dynamic symbol 的 PLT alias 也标成了指向
定义的 code xref。它们是 `ADRP/LDR/ADD/BR X17`，不是 consumer direct call。因此正确
结果为：

| 指标 | 数量 |
| --- | ---: |
| MANIFEST 目标入口 | 114 |
| 全部 direct code-xref 站点 | 349 |
| IDA 主实现簇外 code-xref 原始数 | 305 |
| 其中 `.plt` symbol alias（非 caller） | 2 |
| 主实现簇外真实 direct `BL` 站点 | 303 |
| 有外部 direct caller 的目标入口 | 15 |
| 外部 caller FDE | 25 |
| 去重后的 caller-function → target-function 对 | 71 |

303 个真实站点中有 147 个落到 `PSBRawOwner_dtor_guess@0x598B3C`，这是 raw holder
临时对象在大 consumer 中反复展开的正常清理边；不能把站点数直接解释为 303 个独立
源码调用链。被剔除的 `0x40CD20 → 0x599174` 与 `0x423250 → 0x59B7E8` 分别是
`vector<string>::reserve` 和 `_M_emplace_back_aux<string &>` 的 PLT/dynsym alias；完整
relocation、vtable/lifetime 与 canonical direct-call 门禁见
[FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md)。
其余入口按 consumer 家族归组后覆盖：

- `Motion_Player_findSource@0x6948E8` 与 atlas/resource builder
  `sub_695DE8@0x695DE8`；
- `ResourceManager_loadResource@0x6A8D8C`、
  `Motion_ResourceManager_isExistMotion@0x6A96F8`、
  `Motion_ResourceManager_findMotion@0x6A9ED4` 和
  `Motion_ResourceManager_findSource@0x6AAB3C`；
- `ObjSource` 的 origin/width/height/clip/drawLayer 六个 accessor/method
  `0x69D014..0x69D6D8`，以及 texture materialization `sub_6DA454@0x6DA454`；
- `ttstr_IndexOfChar_guess@0x59A284` 的通用 storage/string consumer，以及少量
  ObjSource/资源对象构造析构辅助函数。两只 `vector<string>` 入口属于前述 PLT alias，
  不再列作反向 consumer。

前两组和 texture materialization 已分别由既有 PlayerResource、ResourceManager、
SourceCache follow-up 闭合；本轮继续核对第三组剩余的四个薄 consumer。完整普查没有发现
绕过当前 raw-node holder、另建第二套 PSB owner/container 的外部调用面。

## Fresh Android 证据

### `width@0x69D19C` / `height@0x69D27C`

两函数的 fresh IDA MCP `decompile` 只有 key 不同；关键伪代码为：

```text
if source.GetTypeCategory() != 7: return 32
tmp = source.GetDictionaryValueStrict("width" or "height")
result = tmp.GetInt()
destroy(tmp)
return result
```

因此默认 32 只属于“source 不是 Dictionary”；Dictionary 缺少 width/height 时必须沿 strict
getter 抛出，不能把 missing key 也归一化为 32。本地
`cpp/plugins/motionplayer/SourceCache.h:128-138` 逐项保持该 category gate、strict getter、
临时 raw node 和整数读取顺序。

### `clip@0x69D35C`

关键伪代码（9 行）：

```text
clip = default_raw_node()
if source.category != 7 || !source.TryGet("clip", clip): return Void
dict = ncbDictionaryAccessor()
for key in ["left", "top", "right", "bottom"]:
    tmp = clip.GetDictionaryValueStrict(key)
    value = tmp.GetDouble()
    dict.SetValue(UTF16(key), value, TJS_MEMBERENSURE, per_key_hint)
    destroy(tmp)
return Variant(dict.dispatch, dict.dispatch)
```

`clip` 自身使用 non-throwing try-get；一旦命中，四个 child 都是 strict getter。fresh
反编译还确认顺序固定为 left→top→right→bottom，并在每次 SetValue 后销毁该 child
临时 raw node。本地 `SourceCache.cpp:356-379` 使用同一 gate、读取次序、四只独立 hint、
`TJS_MEMBERENSURE` 与 dispatch/object-this 相同的返回 Variant。

### `drawLayer@0x69D6D8` 与严格 Layer 转换 `0xA7A050`

关键伪代码（8 行）：

```text
if source.GetTypeCategory() != 7: return
ensureTexture(this)
if target.type != Object: convert target to Object or throw
layer = null
if target.dispatch != null && NativeInstanceSupport(GETINSTANCE, LayerClassID, &layer) fails:
    throw TVPSpecifyLayer
layer.AssignTexture(this.texture)
layer.SetSize(this.texture.width, this.texture.height)
```

`0xA7A050` 对 non-Object 先走 Variant 的严格 Object 转换；空 dispatch 不抛
`TVPSpecifyLayer`，而是把 null layer 原样返回给 caller，随后 `0x69D6D8` 解引用。也就是说，
目标没有本地安全 null gate。`SourceCache.cpp:492-507` 以
`AsObjectNoAddRef → NativeInstanceSupport → AssignTexture → SetSize` 保留同一 first-fault、
异常和调用顺序。

## `drawLayer` 的 by-value 参数正证据

本轮还从注册点追到通用 NCB wrapper，排除“本地签名只是任选 const-reference”的歧义：

```text
ObjSource_ncb_registerMembers@0x69CCB8
  -> ncb_addMember("drawLayer", method-object)
  -> generic method FuncCall@0x6E45D8
  -> argument extractor@0x6E4734 copies *param[0] into a tTJSVariant native argument
  -> Motion_ObjSource_drawLayer@0x69D6D8
  -> wrapper destroys the copied tTJSVariant after the native call
```

`ObjSource` 类注册入口 `0x6FE610` 把该 member set 安装到 `Motion.ObjSource`。本地
`cpp/plugins/motionplayer/main.cpp:33-42` 由 `NCB_METHOD(drawLayer)` 注册，
`SourceCache.h:145-146` / `SourceCache.cpp:492` 明确使用
`void drawLayer(tTJSVariant target)`。本地 ncbind 的 `paramsFunctor` 也按方法形参类型选择
convertor（`cpp/core/plugin/ncbind.hpp:926-965`）；by-value `tTJSVariant` 因而建立并在调用后
销毁独立副本，与 Android wrapper 的对象生命周期一致。

## 结论

- 这组四个 ObjSource consumer 没有新行为或结构 GAP；现有实现逐项匹配。
- `drawLayer` 的 by-value Variant 参数现在由注册→wrapper→copy helper→native call→析构的
  完整 Android 调用链证明，不再只是依据本地宏或 C++ 偏好选择。
- 全 114 入口的外部 caller 面已由 ELF 机械复核；后续调查可直接从 15 个有真实 direct
  caller 的目标和 25 个 caller FDE 继续收敛，而无需再把单个 negative grep 当作“没有
  consumer”，也不能把 PLT alias 当成反向源码调用。
- 本轮只补证据和文档，不修改 `cpp/`，所以不改变
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
