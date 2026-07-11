---
name: kagparserex-store-restore
description: KAGParserEx Store/Restore ([save]/[load]) MacroArgs 序列化格式——save 含 names-array(second)，load 从 [key,value] 扁平对重建 {dict, names-array}
metadata:
  type: project
---

KAGParserEx ([save]/[load] 状态序列化) save 格式**确实**序列化 names-array(second 字段)，load 从中重建。

事实链（全二进制各一对；有多份编译拷贝，逻辑同，下用 0x54A688/0x54BB80 一对，带 paramMacros 故为 Ex 版）：

- **Store** `KAGParserEx_Store@0x54A688`：写 "macros"/"paramMacros"/"macroArgs"/"callStack"/"storageName"/"curLine"/"macroArgStackDepth" 等 PropSet(vtable[48])。
  - MacroArgs 循环 0x54a918：上界=`*(parser+72)`(macroArgStackDepth, 非 vector size)；元素基址=`*(parser+48)`，16B stride；每个 elem 经 `serializeMacroArg(v27, elem)` 转一个 variant 推进 macroArgs Array(SetProp idx)。
- **serializeMacroArg** `@0x54B09C`：`new Array`; count=`sub_98B034(elem[8])`(=second/names-array 的元素数, deque count magic 329^-1); 遍历 names-array → 每个 key; 用 key 在 elem[0](first/values-dict) PropGet(vtable[32]) 取 value; push `[key, value]` 2-elem 数组。**故 save 的每个 MacroArgs 元素 = Array of [key,value] pairs，key 来自 names-array(second)**。
- **Restore** `KAGParserEx_Restore@0x54BB80`：macroArgs 段 0x54bca0：读 "macroArgs" Array → count; 清空旧 vector(release 每元素 [0]和[1]); 读 "macroArgStackDepth"; 循环把每个 saved elem(=[k,v] pairs array) 经 `MacroArg_construct_from_saved`/`sub_559804(realloc)` push 进 vector(parser+48, +16/elem)。
- **MacroArg_construct_from_saved** `@0x54EFDC`：`elem[0]=new Dictionary`(first), `elem[1]=new Array`(second/names-array); count=`sub_98B034(saved_array)`; for step 2: PropGet(2i)=key, PropGet(2i+1)=value → `MacroArg_addPair(elem,key,value)`。
- **MacroArg_addPair** `@0x54F120`：`elem[0].PropSet(vtable[80], key_str, value)`→value 进 dict; key push 进 `elem[1]` names-array(qword_1AB27E0)。**故 Restore 重建 first(dict) 和 second(names-array) 两个字段，names-array 从 save 的 key 重填，非空**。

**copy assignment**（单元素 copy = `MacroArg_copy@0x54ED08`，也被 move_range@0x558D88 复用）：`dst[0]=new Dict`+DicAssign(qword_1AB27C8 vtable[16])深拷 src[0]; `dst[1]=new Array`+Array-Assign(qword_1AB27D8 vtable[16],末参=1)拷 src[1]。**copy 同时拷 first 和 second**。

element 偏移：+0=first(values-dict dispatch), +8=second(names-array dispatch)。16B/elem。

结论：MacroArgs save/load/copy 三处**都处理 names-array(second)**。本地"只存单 dict、load 时从 dict 枚举重建 names-array"的做法偏离——.so save 显式保存 names 顺序(作为 [key,value] 对序列)，load 按保存顺序重填 names-array。
