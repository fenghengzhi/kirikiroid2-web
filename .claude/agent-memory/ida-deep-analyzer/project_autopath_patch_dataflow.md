---
name: autopath-patch-dataflow
description: libkrkr2.so 启动→autopath→patch 完整数据流; autopath 100%脚本驱动, 无引擎内建 patchN.xp3/sibling/分辨率归档自动加; 后加入胜出
metadata:
  type: project
---

# libkrkr2.so autopath / patch.xp3 数据流权威结论

## 核心事实
- 全编码搜索: `patch.xp3`/`patch2.xp3`/`patch%1.xp3`/`data1080`/`scenario.xp3`/任何 `.xp3` 字面量/`AddAutoPath`/`AutoPath` 字符串 = **0 命中**。引擎里"patch"唯一实体是脚本 `patch.tjs`(UTF-8 @0x1511866), 不是归档。
- autopath 容器 `xmmword_1AE4348` = std::vector<ttstr> (begin@0/end@8/cap@qword_1AE4358)。全代码引用仅 4 处:
  - `sub_42FABC`(@0x42FABC) C++ 全局静态 init, 只清零三张表(autopath vector / placed-path 表 qword_1AE4360 / name→archive 哈希 qword_1AE5180), 不 add 任何路径。
  - `TVPAddAutoPath`@0x8EB4B4 — 唯一写入者
  - `TVPRemoveAutoPath`@0x8EB690
  - `TVPGetPlacedPath`@0x8EB8A0 — 读取/解析

## 调用链
sub_913358(App run) → sub_8E2B28(TVPInitScriptEngine 注册 System/Storages/... 到 TJS, 处理 -startup/-debug, 不 add autopath)
→ sub_8E71A8 → TVPExecuteStartupScript@0x8E4C24:
  ① patch.tjs (缺失→msgbox startup_patch_fail + TVPOpenPatchLibUrl)
  ② startup.tjs (默认名 byte_1506A57, 或 -startup 覆盖 qword_1AE2FD8) ← 游戏脚本在此调 Storages.addAutoPath 入列归档
  ③ AfterStartup.tjs

## addAutoPath / 去重
Storages.addAutoPath native @0x8EDF80 → TVPAddAutoPath@0x8EB4B4:
  v8 = TVPAutoPathList_find@0x68C304 (std::find, ttstr 相等); if(end==v8) push_back; byte_1AF3198=0(失效缓存)
→ **去重存在(找到不push), 不重排(永远append到尾)**

## 后加入胜出 (反编译证据)
TVPGetPlacedPath@0x8EB8A0 缓存失效时重建 name→archive 哈希 qword_1AE5180:
  for(v35=begin; v35!=end; ++v35)  // 0x8EBB10 前→后遍历
    for(归档内每个文件 name) TVPPlacedPathMap_insert@0x8EF300(map,&name,hash,v35)
sub_8EF300 命中已有 key(0x8EF458 wcscmp 分支): Release(slot+16 旧值); *(slot+16)=新值  // 覆盖
→ 前→后遍历 + 命中覆盖 ⇒ **列表靠后的归档对同名文件胜出(后加入胜出)**, 与本地 TVPAutoPathTable.Add 覆盖语义一致

## Web 移植偏离 (明确)
- 本地 autoMountSiblingXp3 (cpp/core/environ/web/Platform.cpp:335) 用 readdir 顺序批量灌所有 xp3 = **Web 独有偏离**, libkrkr2.so 无此逻辑。
- 无分辨率归档自动补加: "resolution"字符串全来自 Cocos2d/GdiPlus/OpenCV/cairo 库, 与 xp3 无关。本地 hack 注释"为 data1080.xp3 自动可用"动机在原版无对应实现。
- 1:1 复刻关键不变量: 主数据 vs patch/scenario 归档的 addAutoPath 相对顺序必须复刻原 startup.tjs 调用顺序(后者胜出); readdir 顺序≠脚本顺序会改变同名资源覆盖关系。

## 其他
- TVPNormalizeStorageName_guess@0x8E8FF0: 规范化 \→/, 抽 protocol 前缀, 解析 .., 用 qword_1AE2FF8(project-dir 默认 base) 兜底。
- 已 rename+idb_save: 上述 9 个函数符号。
