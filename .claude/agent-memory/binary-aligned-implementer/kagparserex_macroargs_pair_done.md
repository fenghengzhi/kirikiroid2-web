---
name: kagparserex-macroargs-pair-done
description: KAGParser MacroArgs 从单 values-dict 重构为 {values-dict, names-array} 配对 vector，忠实复刻 KAGParserExb.cpp
metadata:
  type: project
---

# KAGParserEx MacroArgs 容器复原 DONE (2026-06-17)

源文件 `../../src/plugins/KAGParserExb.cpp`（= KAGParserEx，并入 libkrkr2.so）。
本地 `cpp/core/base/KAGParser.{h,cpp}`。

## 容器结构（强证据，勿推翻）
MacroArgs = `std::vector<std::pair<iTJSDispatch2* first, iTJSDispatch2* second>>`，16B 元素：
- +0 = first = values-dict（宏实参字典）
- +8 = second = names-array（按源码顺序的属性名 Array，与 DicObj/TagList 同构）
- `.so` base=parser+56/+48、depth=parser+80/+72（两组函数 dispatch 偏移差 8B = 同一 MacroArgStackDepth 字段，非两个字段）。
- 解析器布局：*(parser+16)=DicObj、*(parser+24)=TagList。push 源 = {DicObj, TagList}。

## 关键裁决
- **GetMacroTopNoAddRef 返回 element[depth-1].first（+0=values-dict），不是 second。**（getter @0x55FAEC/@0x55FBF4）
- **save 格式包含 names-array**：Store 把 second 的有序 key 与对应 value 扁平为 [k0,v0,k1,v1,...]；Restore 重填 second 非空。旧实现"单 dict / load 从 dict 重新枚举"已纠正。
- `*` 转发分支（@0x564080）按 second(names-array) 的 PropGetByNum 有序枚举（非 EnumMembers values-dict 的 hash 序）。旧 `tKAGMacroAllCallback` EnumMembers 回调已删除——它是平台边界替代，现已被容器复刻消除（analysis §3.6 已纠正）。
- `*` 分支 DicObj.PropSetByVS flag = 单 TJS_MEMBERENSURE(512)，**不带** TJS_IGNOREPROP（@0x564208；审计 catch 的唯一偏差，已修）。

## .so↔本地 函数映射
| .so | 地址 | 本地 |
|---|---|---|
| Push 内联+sub_569A18新建/sub_5698BC复用 | 0x5666c0 / 0x569A18 / 0x5698BC | PushMacroArgs(values,names) |
| Clear 内联(clear sub_55C414) | 0x55C4AC | ClearMacroArgs（双 Release first+second） |
| Pop sub_55EFD8 | 0x55EFD8 | PopMacroArgs |
| GetTop getter | 0x55FAEC/0x55FBF4 | GetMacroTopNoAddRef（.first） |
| `*` 分支 | 0x564080 | KAGParser.cpp `*` 分支 names PropGetByNum |
| Store macroArgs+serializeMacroArg | 0x54A918 / 0x54B09C | Store macroArgs 段 |
| Restore+construct_from_saved+addPair | 0x54BCA0 / 0x54EFDC / 0x54F120 | Restore macroArgs 段 |
| copy MacroArg_copy | 0x54ED08 | operator= copy MacroArgs 段 |

## 新增类成员（取法同 DicAssign）
- ArrayAssign = Array class "assign"（qword_1AB3C10），Push 深拷 names→second、copy 拷 second。
- ArrayPush = Array class "push"（qword_1AB27E0），Store serialize 一次 push(key,value) 两元素到扁平数组。本地 Array "add" 只单元素不够用。
- ctor 取 + 异常释放 + Invalidate 释放，三处都要同步（all-or-nothing）。

## 验证
- web/debug + krkr2_wasmtime_guest 两 target clean 链接（KAGParser 是 core，guest 子集 target 链接到，必须双验否则 CI 红）。
- binary-alignment-auditor：19/20 检查项 ✅，唯一偏差（`*` 分支 IGNOREPROP）已修复后重建通过。
- 无回归：MacroArgs 路径对 logo 序列 inert（logo 无 [macro] 录制、depth=0、`*` 分支不进）；logo taglist 主路径（普通属性循环 TagListAddName + AttachTagList）diff 隔离零改动。无带宏 scenario 的现成 fixture（[mono_base *] 属 scenario 级，dracu_boot 启动包不含）→ 运行期验证留缺口，按 CLAUDE.md 不造 fixture。
