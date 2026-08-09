# psbfile 逐函数审计 canonical TASK_TREE（114）

## 口径

- 唯一权威来源：Android kirikiroid2 `libkrkr2.so` 内嵌 psbfile 实现。
- `ROOT` 是调度用的合成根，不对应函数 agent；主 agent 只直接启动它的两个函数子节点。
- 每一行 `NODE` 都对应一个且仅一个 function subagent；`parent=@ADDR` 是该节点唯一的 canonical parent。
- 父边类型只使用任务约定的八种标签。`[classification-only]` 明确表示逻辑归组，不暗示调用。
- 非 canonical DAG 边只在“Cross-reference”中以 `@ADDR` 记录；那里没有 `0x...` 函数节点，因此不会重复派发。

## Canonical spanning tree

- NODE `0x42CEF8` PSBFile_ncbClassInfo_static_init `[classification-only]` parent=`ROOT`
  - NODE `0x597F24` ncbClassInfo_PSBFile_InfoCtor_guess `[lifecycle]` parent=`@42CEF8`
    - NODE `0x597E98` ncbClassInfo_PSBFile_GetName_guess `[helper]` parent=`@597F24`
    - NODE `0x597EA8` ncbClassInfo_PSBFile_GetID_guess `[helper]` parent=`@597F24`
    - NODE `0x597EB8` ncbClassInfo_PSBFile_GetClassObject_guess `[helper]` parent=`@597F24`
    - NODE `0x597EC8` ncbClassInfo_PSBFile_IsSubClass_guess `[helper]` parent=`@597F24`
    - NODE `0x597ED0` ncbClassInfo_PSBFile_Set_guess `[helper]` parent=`@597F24`

- NODE `0x42CF28` psbfile_static_init `[classification-only]` parent=`ROOT`
  - NODE `0x59849C` PSBFile_preRegister_guess_initPsbFile `[registration]` parent=`@42CF28`
    - NODE `0x599830` PSBMedia_deletingDestructor_guess `[vtable-member]` parent=`@59849C`
      - NODE `0x5997F0` PSBMedia_completeDestructor_guess `[lifecycle]` parent=`@599830`
    - NODE `0x599878` PSBMedia_AddRef_guess `[vtable-member]` parent=`@59849C`
    - NODE `0x599888` PSBMedia_Release_guess `[vtable-member]` parent=`@59849C`
    - NODE `0x5998A8` PSBMedia_GetName_guess `[vtable-member]` parent=`@59849C`
    - NODE `0x5998BC` PSBMedia_NormalizeDomainName_nullsub `[vtable-member]` parent=`@59849C`
    - NODE `0x5998C0` PSBMedia_NormalizePathName_nullsub `[vtable-member]` parent=`@59849C`
    - NODE `0x5998C4` PSBMedia_CheckExistentStorage_guess `[vtable-member]` parent=`@59849C`
      - NODE `0x599E04` PSBMedia_EnsureContainer_guess `[direct-call]` parent=`@5998C4`
        - NODE `0x59A284` ttstr_IndexOfChar_guess `[direct-call]` parent=`@599E04`
        - NODE `0x59A330` ncbInstanceAdaptor_PSBFFile_CreateAdaptor_guess `[direct-call]` parent=`@599E04`
      - NODE `0x59A0B4` PSBMedia_GetResourceData_guess `[direct-call]` parent=`@5998C4`
        - NODE `0x5996E4` PSBRawNode_GetResource_guess `[helper]` parent=`@59A0B4`
    - NODE `0x59993C` PSBMedia_Open_guess `[vtable-member]` parent=`@59849C`
    - NODE `0x5999F4` PSBMedia_GetListAt_guess `[vtable-member]` parent=`@59849C`
      - NODE `0x59A4B0` PSBMedia_Resolve_guess `[direct-call]` parent=`@5999F4`
        - NODE `0x5995D8` PSBRawNode_ContainsDictionaryKey_guess `[direct-call]` parent=`@59A4B0`
          - NODE `0x598D58` PSBRawNode_GetDictionaryValue_guess `[direct-call]` parent=`@5995D8`
        - NODE `0x598C58` PSBRawNode_GetDictionaryValueStrict_guess `[direct-call]` parent=`@59A4B0`
    - NODE `0x599DD8` PSBMedia_GetLocallyAccessibleName_guess `[vtable-member]` parent=`@59849C`

  - NODE `0x59A8D8` PSBFile_AutoRegister_Regist_guess `[registration]` parent=`@42CF28`
    - NODE `0x59AA84` PSBFile_RegistBegin_guess `[direct-call]` parent=`@59A8D8`
      - NODE `0x59ABD8` PSBFile_instanceAdaptor_CreateEmpty_ctor_guess `[registration]` parent=`@59AA84`
        - NODE `0x59AC04` PSBFile_ncbFinalizeEmptyCallback_guess `[helper]` parent=`@59ABD8`
        - NODE `0x59AC0C` PSBFile_ncbInstanceAdaptor_Invalidate_guess `[vtable-member]` parent=`@59ABD8`
        - NODE `0x59AD08` PSBFile_ncbInstanceAdaptor_deletingDestructor_guess `[vtable-member]` parent=`@59ABD8`
          - NODE `0x59AC7C` PSBFile_ncbInstanceAdaptor_completeDestructor_guess `[lifecycle]` parent=`@59AD08`
    - NODE `0x597F38` PSBFile_ncb_registerMembers_guess `[direct-call]` parent=`@59A8D8`
      - NODE `0x59AEEC` PSBFile_RegistItem_guess `[direct-call]` parent=`@597F38`
        - NODE `0x59B14C` PSBFile_factory_FuncCall_guess `[registration]` parent=`@59AEEC`
          - NODE `0x5980F4` PSBFile_Factory_guess `[direct-call]` parent=`@59B14C`
            - NODE `0x598268` PSBFile_Load `[direct-call]` parent=`@5980F4`
              - NODE `0x598538` PSBFile_LoadStorage_guess `[direct-call]` parent=`@598268`
                - NODE `0x598708` PSBFile_Adopt_guess `[direct-call]` parent=`@598538`
                  - NODE `0x598AAC` PSBRawOwner_ctor_guess `[lifecycle]` parent=`@598708`
                    - NODE `0x598B3C` PSBRawOwner_dtor_guess `[lifecycle]` parent=`@598AAC`
                  - NODE `0x598960` PSBRawOwner_Refresh_guess `[helper]` parent=`@598708`
              - NODE `0x598A3C` PSBFile_GetRoot_guess `[classification-only]` parent=`@598268`
                - NODE `0x598E44` PSBRawNode_IsValid_guess `[classification-only]` parent=`@598A3C`
                - NODE `0x598E64` PSBRawNode_GetDictionaryKeys_guess `[classification-only]` parent=`@598A3C`
                  - NODE `0x599174` std_vector_string_reserve `[stl-instantiation]` parent=`@598E64`
                  - NODE `0x59B7E8` std_vector_string_emplace_back_aux `[stl-instantiation]` parent=`@598E64`
                - NODE `0x598B58` PSBRawNode_GetString_guess `[classification-only]` parent=`@598A3C`
                - NODE `0x5992E8` PSBRawNode_GetDouble_guess `[classification-only]` parent=`@598A3C`
                - NODE `0x599438` PSBRawNode_GetInt_guess `[classification-only]` parent=`@598A3C`
                - NODE `0x599554` PSBRawNode_GetTypeCategory_guess `[classification-only]` parent=`@598A3C`
              - NODE `0x598A64` PSBFile_Transfer_guess `[classification-only]` parent=`@598268`
          - NODE `0x59B268` PSBFile_ncbFactory_deletingDestructor_guess `[vtable-member]` parent=`@59B14C`
        - NODE `0x59B28C` PSBFile_root_PropGet_guess `[registration]` parent=`@59AEEC`
          - NODE `0x59B48C` PSBFile_root_typed_Invoke_guess `[direct-call]` parent=`@59B28C`
            - NODE `0x5981F8` PSBFile_GetRootDispatch_guess `[direct-call]` parent=`@59B48C`
              - NODE `0x597AD4` PSBValueDispatch_ctor_guess `[lifecycle]` parent=`@5981F8`
                - NODE `0x596BC4` PSBValueDispatch_getString_guess `[helper]` parent=`@597AD4`
                - NODE `0x596C70` PSBValueDispatch_getResource_guess `[helper]` parent=`@597AD4`
                - NODE `0x5975C0` PSBValueDispatch_decodeName_guess `[helper]` parent=`@597AD4`
                - NODE `0x597AC0` PSBValueDispatch_AddRef `[vtable-member]` parent=`@597AD4`
                - NODE `0x597A40` PSBValueDispatch_Release `[vtable-member]` parent=`@597AD4`
                - NODE `0x597A20` PSBValueDispatch_FuncCall `[vtable-member]` parent=`@597AD4`
                - NODE `0x597A18` PSBValueDispatch_FuncCallByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x597854` PSBValueDispatch_PropGet `[vtable-member]` parent=`@597AD4`
                  - NODE `0x59641C` PSB_FindNameIndex_guess `[direct-call]` parent=`@597854`
                  - NODE `0x59659C` PSB_FindDictionaryValueOffset_guess `[direct-call]` parent=`@597854`
                  - NODE `0x59673C` PSBValueDispatch_CreateVariant_guess `[direct-call]` parent=`@597854`
                - NODE `0x5976C4` PSBValueDispatch_PropGetByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x5976BC` PSBValueDispatch_PropSet `[vtable-member]` parent=`@597AD4`
                - NODE `0x5976B4` PSBValueDispatch_PropSetByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x5975E0` PSBValueDispatch_GetCount `[vtable-member]` parent=`@597AD4`
                - NODE `0x5975D8` PSBValueDispatch_GetCountByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x5975D0` PSBValueDispatch_PropSetByVS `[vtable-member]` parent=`@597AD4`
                - NODE `0x596F50` PSBValueDispatch_EnumMembers `[vtable-member]` parent=`@597AD4`
                  - NODE `0x597B1C` PSB_DecodeName_guess `[direct-call]` parent=`@596F50`
                - NODE `0x596F48` PSBValueDispatch_DeleteMember `[vtable-member]` parent=`@597AD4`
                - NODE `0x596F40` PSBValueDispatch_DeleteMemberByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x596F0C` PSBValueDispatch_Invalidate_dispatch `[vtable-member]` parent=`@597AD4`
                - NODE `0x596F04` PSBValueDispatch_InvalidateByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x596EF0` PSBValueDispatch_IsValid `[vtable-member]` parent=`@597AD4`
                - NODE `0x596EE8` PSBValueDispatch_IsValidByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x596EE0` PSBValueDispatch_CreateNew `[vtable-member]` parent=`@597AD4`
                - NODE `0x596ED8` PSBValueDispatch_CreateNewByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x596ED0` PSBValueDispatch_Reserved1 `[vtable-member]` parent=`@597AD4`
                - NODE `0x596E24` PSBValueDispatch_IsInstanceOf `[vtable-member]` parent=`@597AD4`
                - NODE `0x596E1C` PSBValueDispatch_IsInstanceOfByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x596E14` PSBValueDispatch_Operation `[vtable-member]` parent=`@597AD4`
                - NODE `0x596E0C` PSBValueDispatch_OperationByNum `[vtable-member]` parent=`@597AD4`
                - NODE `0x596D90` PSBValueDispatch_NativeInstanceSupport `[vtable-member]` parent=`@597AD4`
                - NODE `0x596D88` PSBValueDispatch_ClassInstanceInfo `[vtable-member]` parent=`@597AD4`
                - NODE `0x596D80` PSBValueDispatch_Reserved2 `[vtable-member]` parent=`@597AD4`
                - NODE `0x596D78` PSBValueDispatch_Reserved3 `[vtable-member]` parent=`@597AD4`
                - NODE `0x597A30` PSBValueDispatch_Construct_primary `[vtable-member]` parent=`@597AD4`
                  - NODE `0x597A38` PSBValueDispatch_Construct_secondary_thunk `[vtable-member]` parent=`@597A30`
                - NODE `0x596F38` PSBValueDispatch_native_Invalidate_primary_nullsub `[vtable-member]` parent=`@597AD4`
                  - NODE `0x596F3C` PSBValueDispatch_native_Invalidate_secondary_nullsub `[vtable-member]` parent=`@596F38`
                - NODE `0x597A28` PSBValueDispatch_native_Destruct_primary_nullsub `[vtable-member]` parent=`@597AD4`
                  - NODE `0x597A2C` PSBValueDispatch_native_Destruct_secondary_nullsub `[vtable-member]` parent=`@597A28`
          - NODE `0x59B378` PSBFile_root_PropSet_guess `[vtable-member]` parent=`@59B28C`
          - NODE `0x59B460` PSBFile_rootProperty_deletingDestructor_guess `[vtable-member]` parent=`@59B28C`
          - NODE `0x59B484` PSBFile_root_GetFlags_guess `[vtable-member]` parent=`@59B28C`
        - NODE `0x59B570` PSBFile_load_FuncCall_guess `[registration]` parent=`@59AEEC`
          - NODE `0x59B708` PSBFile_load_typed_Invoke_guess `[direct-call]` parent=`@59B570`
          - NODE `0x59B6DC` PSBFile_loadMethod_deletingDestructor_guess `[vtable-member]` parent=`@59B570`
          - NODE `0x59B700` PSBFile_load_GetFlags_guess `[vtable-member]` parent=`@59B570`
    - NODE `0x59AD84` PSBFile_RegistEnd_guess `[direct-call]` parent=`@59A8D8`
      - NODE `0x59AEE4` PSBFile_ncbDummyConstructorNotImpl_guess `[registration]` parent=`@59AD84`

  - NODE `0x59A968` PSBFile_AutoRegister_Unregist_guess `[lifecycle]` parent=`@42CF28`
    - NODE `0x597F08` ncbClassInfo_PSBFile_Clear_guess `[helper]` parent=`@59A968`

## Cross-reference（非 canonical 边；不得重复派发）

- `@59A968` unregister body 也使用 `@597F38`；fresh decompile 证明 class-info Clear
  shape 被内联，`@597F08` 没有 xref，因此 child 边是 `[helper]`，不是 direct-call。
- `@5975C0` 直接调用 `@597B1C`；后者的 canonical parent 选为另一个 Android 直接 caller `@596F50`。
- `@5976C4` 与 `@596F50` 都直接调用 `@59673C`；其 canonical parent 选为 `@597854`。
- `@59673C` 的 String/Resource 分支分别是 `@596BC4`、`@596C70` 的完整 O3 内联克隆，
  提供源码级调用正证据；两 helper 的 canonical parent 仍保留为 dispatch 构造归属
  `@597AD4`，不因非 canonical inline 边重复派发。
- `@598C58`、`@598D58` 都直接调用 `@59641C` 与 `@59659C`；两 helper 的 canonical parent 选为 `@597854`。
- `@598E64` 直接调用 `@597B1C`；其 canonical parent 选为 `@596F50`。
- `@598268` 的 Octet 分支直接进入 `@598708`；canonical tree 采用 String 分支 `@598268` → `@598538` → `@598708`。
- `@599E04` 还直接调用 `@598538`；后者留在 Factory/Load 链的 canonical parent `@598268` 下。
- `@59993C` 直接调用 `@599E04`、`@59A0B4`；两者的 canonical parent 选为另一个直接 caller `@5998C4`。
- `@5999F4` 直接调用 `@599E04`、`@597B1C`；canonical parent 分别为 `@5998C4`、`@596F50`。
- `@59A4B0` 还直接调用 `@59A284`；后者的 canonical parent 选为 `@599E04`。
- `@59A330` 通过 class-object CreateNew vdispatch 进入 `@59B14C`；wrapper 的 canonical parent 保留为注册者 `@59AEEC`。
- `@59B708` 只执行首个 `tTJSVariant` 的按值抽取/复制；load wrapper 随后经 member
  pointer 间接进入 `@598268`。后者的 canonical parent 保留为直接 caller `@5980F4`。
- `@5981F8` 内联构造与 `@597AD4` 相同的 dispatch shape；canonical 边标成 lifecycle，不伪装为保留的直接调用。
- `@598708` 内联 owner 构造 shape，并仅在 filter 非空时执行 strict refresh shape；对应 canonical 边分别标成 lifecycle/helper，不伪装为直接调用。
- `@42CF28` 只构造两只 autoreg 对象；后续 LoadModule 才分别 vdispatch 到 `@59849C`、`@59A8D8`，canonical 边标成 registration。

## 调度协议

1. 函数 agent 阶段 A 只审计自己这一个 `NODE`，fresh `decompile` 后写唯一报告并结束 turn。
2. 主 agent 在阶段 A 完成且有空闲槽位时，使用 `followup_task` 唤醒该 parent。
3. parent 的阶段 B follow-up 只创建上面缩进一级、且 `parent` 指向自己的直接孩子；不得审计第二个函数。
4. 未成功 spawn 的孩子必须原样返回，之后由主 agent再次唤醒；不得标记为完成。
5. 每个报告必须记录本节点的 TASK_TREE parent 与本节 cross-reference，不得为 cross-reference 创建 agent。
