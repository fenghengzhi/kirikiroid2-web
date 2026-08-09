# 逐函数审计 canonical manifest（114）

此表只固定一函数一报告的地址集合；报告存在性和结论由最终汇总脚本机械检查。

| # | 组 | 地址 | canonical 标签 | 唯一报告 |
| ---: | :---: | --- | --- | --- |
| 1 | A | `0x59641C` | PSB_FindNameIndex_guess | [report](functions/0x59641C.md) |
| 2 | A | `0x59659C` | PSB_FindDictionaryValueOffset_guess | [report](functions/0x59659C.md) |
| 3 | A | `0x597AD4` | PSBValueDispatch_ctor_guess | [report](functions/0x597AD4.md) |
| 4 | A | `0x59673C` | PSBValueDispatch_CreateVariant_guess | [report](functions/0x59673C.md) |
| 5 | A | `0x596BC4` | PSBValueDispatch_getString_guess | [report](functions/0x596BC4.md) |
| 6 | A | `0x596C70` | PSBValueDispatch_getResource_guess | [report](functions/0x596C70.md) |
| 7 | A | `0x5975C0` | PSBValueDispatch_decodeName_guess | [report](functions/0x5975C0.md) |
| 8 | A | `0x597AC0` | [vslot 00] AddRef | [report](functions/0x597AC0.md) |
| 9 | A | `0x597A40` | [vslot 01] Release | [report](functions/0x597A40.md) |
| 10 | A | `0x597A20` | [vslot 02] FuncCall | [report](functions/0x597A20.md) |
| 11 | A | `0x597A18` | [vslot 03] FuncCallByNum | [report](functions/0x597A18.md) |
| 12 | A | `0x597854` | [vslot 04] PropGet | [report](functions/0x597854.md) |
| 13 | A | `0x5976C4` | [vslot 05] PropGetByNum | [report](functions/0x5976C4.md) |
| 14 | A | `0x5976BC` | [vslot 06] PropSet | [report](functions/0x5976BC.md) |
| 15 | A | `0x5976B4` | [vslot 07] PropSetByNum | [report](functions/0x5976B4.md) |
| 16 | A | `0x5975E0` | [vslot 08] GetCount | [report](functions/0x5975E0.md) |
| 17 | A | `0x5975D8` | [vslot 09] GetCountByNum | [report](functions/0x5975D8.md) |
| 18 | A | `0x5975D0` | [vslot 10] PropSetByVS | [report](functions/0x5975D0.md) |
| 19 | A | `0x596F50` | [vslot 11] EnumMembers | [report](functions/0x596F50.md) |
| 20 | A | `0x596F48` | [vslot 12] DeleteMember | [report](functions/0x596F48.md) |
| 21 | A | `0x596F40` | [vslot 13] DeleteMemberByNum | [report](functions/0x596F40.md) |
| 22 | A | `0x596F0C` | [vslot 14] Invalidate(dispatch overload) | [report](functions/0x596F0C.md) |
| 23 | A | `0x596F04` | [vslot 15] InvalidateByNum | [report](functions/0x596F04.md) |
| 24 | A | `0x596EF0` | [vslot 16] IsValid | [report](functions/0x596EF0.md) |
| 25 | A | `0x596EE8` | [vslot 17] IsValidByNum | [report](functions/0x596EE8.md) |
| 26 | A | `0x596EE0` | [vslot 18] CreateNew | [report](functions/0x596EE0.md) |
| 27 | A | `0x596ED8` | [vslot 19] CreateNewByNum | [report](functions/0x596ED8.md) |
| 28 | A | `0x596ED0` | [vslot 20] Reserved1 | [report](functions/0x596ED0.md) |
| 29 | A | `0x596E24` | [vslot 21] IsInstanceOf | [report](functions/0x596E24.md) |
| 30 | A | `0x596E1C` | [vslot 22] IsInstanceOfByNum | [report](functions/0x596E1C.md) |
| 31 | A | `0x596E14` | [vslot 23] Operation | [report](functions/0x596E14.md) |
| 32 | A | `0x596E0C` | [vslot 24] OperationByNum | [report](functions/0x596E0C.md) |
| 33 | A | `0x596D90` | [vslot 25] NativeInstanceSupport | [report](functions/0x596D90.md) |
| 34 | A | `0x596D88` | [vslot 26] ClassInstanceInfo | [report](functions/0x596D88.md) |
| 35 | A | `0x596D80` | [vslot 27] Reserved2 | [report](functions/0x596D80.md) |
| 36 | A | `0x596D78` | [vslot 28] Reserved3 | [report](functions/0x596D78.md) |
| 37 | A | `0x597A30` | [vslot 29] Construct（main-vtable duplicate） | [report](functions/0x597A30.md) |
| 38 | A | `0x596F38` | [vslot 30] native Invalidate no-op（nullsub_258） | [report](functions/0x596F38.md) |
| 39 | A | `0x597A28` | [vslot 31] native Destruct no-op（nullsub_260） | [report](functions/0x597A28.md) |
| 40 | A | `0x597A38` | [secondary 0] Construct duplicate/thunk | [report](functions/0x597A38.md) |
| 41 | A | `0x596F3C` | [secondary 1] native Invalidate duplicate（nullsub_259） | [report](functions/0x596F3C.md) |
| 42 | A | `0x597A2C` | [secondary 2] native Destruct duplicate（nullsub_261） | [report](functions/0x597A2C.md) |
| 43 | B | `0x597B1C` | PSB_DecodeName_guess | [report](functions/0x597B1C.md) |
| 44 | C | `0x597E98` | [ncb] GetName_guess | [report](functions/0x597E98.md) |
| 45 | C | `0x597EA8` | [ncb] GetID_guess | [report](functions/0x597EA8.md) |
| 46 | C | `0x597EB8` | [ncb] GetClassObject_guess | [report](functions/0x597EB8.md) |
| 47 | C | `0x597EC8` | [ncb] IsSubClass_guess | [report](functions/0x597EC8.md) |
| 48 | C | `0x597ED0` | [ncb] Set_guess | [report](functions/0x597ED0.md) |
| 49 | C | `0x597F08` | [ncb] Clear_guess | [report](functions/0x597F08.md) |
| 50 | C | `0x597F24` | [ncb] InfoCtor_guess | [report](functions/0x597F24.md) |
| 51 | C | `0x597F38` | [ncb] PSBFile_ncb_registerMembers_guess | [report](functions/0x597F38.md) |
| 52 | C | `0x5980F4` | [ncb callback] PSBFile_Factory_guess | [report](functions/0x5980F4.md) |
| 53 | C | `0x5981F8` | [ncb callback] PSBFile_GetRootDispatch_guess | [report](functions/0x5981F8.md) |
| 54 | D | `0x598268` | PSBFile::Load | [report](functions/0x598268.md) |
| 55 | D | `0x598538` | PSBFile::LoadStorage_guess | [report](functions/0x598538.md) |
| 56 | D | `0x598708` | PSBFile::Adopt_guess | [report](functions/0x598708.md) |
| 57 | D | `0x598A3C` | PSBFile::GetRoot_guess | [report](functions/0x598A3C.md) |
| 58 | D | `0x598A64` | PSBFile::Transfer_guess | [report](functions/0x598A64.md) |
| 59 | D | `0x598AAC` | PSBRawOwner_ctor_guess | [report](functions/0x598AAC.md) |
| 60 | D | `0x598960` | PSBRawOwner_Refresh_guess | [report](functions/0x598960.md) |
| 61 | D | `0x598B3C` | PSBRawOwner_dtor_guess | [report](functions/0x598B3C.md) |
| 62 | D | `0x598C58` | GetDictionaryValueStrict_guess | [report](functions/0x598C58.md) |
| 63 | D | `0x598D58` | GetDictionaryValue_guess | [report](functions/0x598D58.md) |
| 64 | D | `0x598E44` | IsValid_guess | [report](functions/0x598E44.md) |
| 65 | D | `0x598E64` | GetDictionaryKeys_guess | [report](functions/0x598E64.md) |
| 66 | D | `0x599174` | [stl] std::vector<std::string>::reserve | [report](functions/0x599174.md) |
| 67 | D | `0x5995D8` | ContainsDictionaryKey_guess | [report](functions/0x5995D8.md) |
| 68 | D | `0x598B58` | GetString_guess | [report](functions/0x598B58.md) |
| 69 | D | `0x5992E8` | GetDouble_guess | [report](functions/0x5992E8.md) |
| 70 | D | `0x599438` | GetInt_guess | [report](functions/0x599438.md) |
| 71 | D | `0x599554` | GetTypeCategory_guess | [report](functions/0x599554.md) |
| 72 | D | `0x5996E4` | GetResource_guess | [report](functions/0x5996E4.md) |
| 73 | E | `0x59849C` | PSBFile_preRegister_guess / local initPsbFile | [report](functions/0x59849C.md) |
| 74 | E | `0x5997F0` | [vslot 00] PSBMedia_completeDestructor_guess | [report](functions/0x5997F0.md) |
| 75 | E | `0x599830` | [vslot 01] PSBMedia_deletingDestructor_guess | [report](functions/0x599830.md) |
| 76 | E | `0x599878` | [vslot 02] PSBMedia_AddRef_guess（non-atomic） | [report](functions/0x599878.md) |
| 77 | E | `0x599888` | [vslot 03] PSBMedia_Release_guess | [report](functions/0x599888.md) |
| 78 | E | `0x5998A8` | [vslot 04] PSBMedia_GetName_guess → `psb` | [report](functions/0x5998A8.md) |
| 79 | E | `0x5998BC` | [vslot 05] NormalizeDomainName no-op（nullsub_262） | [report](functions/0x5998BC.md) |
| 80 | E | `0x5998C0` | [vslot 06] NormalizePathName no-op（nullsub_263） | [report](functions/0x5998C0.md) |
| 81 | E | `0x5998C4` | [vslot 07] CheckExistentStorage_guess | [report](functions/0x5998C4.md) |
| 82 | E | `0x59993C` | [vslot 08] Open_guess | [report](functions/0x59993C.md) |
| 83 | E | `0x5999F4` | [vslot 09] GetListAt_guess | [report](functions/0x5999F4.md) |
| 84 | E | `0x599DD8` | [vslot 10] GetLocallyAccessibleName_guess | [report](functions/0x599DD8.md) |
| 85 | E | `0x599E04` | EnsureContainer_guess | [report](functions/0x599E04.md) |
| 86 | E | `0x59A0B4` | GetResourceData_guess | [report](functions/0x59A0B4.md) |
| 87 | E | `0x59A284` | ttstr_IndexOfChar_guess | [report](functions/0x59A284.md) |
| 88 | E | `0x59A330` | [ncb] ncbInstanceAdaptor<PSBFile>::CreateAdaptor_guess | [report](functions/0x59A330.md) |
| 89 | E | `0x59A4B0` | Resolve_guess | [report](functions/0x59A4B0.md) |
| 90 | F | `0x59A8D8` | [ncb] AutoRegister::Regist_guess | [report](functions/0x59A8D8.md) |
| 91 | F | `0x59A968` | [ncb] AutoRegister::Unregist_guess | [report](functions/0x59A968.md) |
| 92 | F | `0x59AA84` | [ncb] RegistBegin_guess | [report](functions/0x59AA84.md) |
| 93 | F | `0x59ABD8` | [ncb] instance-adaptor CreateEmpty/ctor_guess | [report](functions/0x59ABD8.md) |
| 94 | F | `0x59AC04` | [ncb] PSBFile_ncbFinalizeEmptyCallback_guess | [report](functions/0x59AC04.md) |
| 95 | F | `0x59AC0C` | [ncb] PSBFile_ncbInstanceAdaptor_Invalidate_guess（cleanup/reset） | [report](functions/0x59AC0C.md) |
| 96 | F | `0x59AC7C` | [ncb] PSBFile_ncbInstanceAdaptor_completeDestructor_guess | [report](functions/0x59AC7C.md) |
| 97 | F | `0x59AD08` | [ncb] PSBFile_ncbInstanceAdaptor_deletingDestructor_guess | [report](functions/0x59AD08.md) |
| 98 | F | `0x59AD84` | [ncb] RegistEnd_guess | [report](functions/0x59AD84.md) |
| 99 | F | `0x59AEE4` | [ncb] PSBFile_ncbDummyConstructorNotImpl_guess | [report](functions/0x59AEE4.md) |
| 100 | F | `0x59AEEC` | [ncb] RegistItem_guess | [report](functions/0x59AEEC.md) |
| 101 | F | `0x59B14C` | [ncb vslot] factory FuncCall_guess | [report](functions/0x59B14C.md) |
| 102 | F | `0x59B268` | [ncb vslot] PSBFile_ncbFactory_deletingDestructor_guess | [report](functions/0x59B268.md) |
| 103 | F | `0x59B28C` | [ncb vslot] root PropGet_guess → @59B48C | [report](functions/0x59B28C.md) |
| 104 | F | `0x59B378` | [ncb vslot] root PropSet_guess（access denied） | [report](functions/0x59B378.md) |
| 105 | F | `0x59B460` | [ncb vslot] PSBFile_rootProperty_deletingDestructor_guess | [report](functions/0x59B460.md) |
| 106 | F | `0x59B484` | [ncb vslot] root GetFlags_guess → 0 | [report](functions/0x59B484.md) |
| 107 | F | `0x59B48C` | [ncb] root typed Invoke_guess → @5981F8 | [report](functions/0x59B48C.md) |
| 108 | F | `0x59B570` | [ncb vslot] load FuncCall_guess → @59B708 | [report](functions/0x59B570.md) |
| 109 | F | `0x59B6DC` | [ncb vslot] PSBFile_loadMethod_deletingDestructor_guess | [report](functions/0x59B6DC.md) |
| 110 | F | `0x59B700` | [ncb vslot] load GetFlags_guess → 0 | [report](functions/0x59B700.md) |
| 111 | F | `0x59B708` | [ncb] load CopyFirstArgument_guess；本体只复制首参数，父包装器 @59B570 随后经已注册 member-pointer BLR → @598268 | [report](functions/0x59B708.md) |
| 112 | G | `0x59B7E8` | [stl] std::vector<std::string>::_M_emplace_back_aux<std::string &> | [report](functions/0x59B7E8.md) |
| 113 | H | `0x42CEF8` | PSBFile_ncbClassInfo_static_init | [report](functions/0x42CEF8.md) |
| 114 | H | `0x42CF28` | psbfile_static_init | [report](functions/0x42CF28.md) |
