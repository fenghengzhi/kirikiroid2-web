"""Per-family target address catalog for the Frida call tracer.

Offsets are load-base relative (applied on device as
`Module.findBaseAddress('libkrkr2.so').add(off)`).

Keep the list minimal: only sub-functions on the critical path so the
trace doesn't drown in low-signal variant/helper noise. Callees like
`sub_A0F5E0` / `sub_A0F778` (tTJSVariant ctor/dtor) fire tens of times
per case — include them only if we need to debug variant lifetimes.
"""

# --- bezier_curve: sub_69A754 (cubic Bézier over TJS-wrapped arrays) ----
# Decompile at 0x69A754 shows critical path:
#   sub_56C694(curve)    → control-point count
#   sub_6695BC(xs, i)    → x control point
#   sub_6695BC(ys, i)    → y control point
BEZIER_CURVE_TARGETS = [
    0x69A754,   # sub_69A754 — entry
    0x56C694,   # tTJSArray length
    0x6695BC,   # tTJSArray indexed-as-double
]

# --- position_interp: sub_69A4D4 -----------------------------------------
# Decompile at 0x69A4D4 shows:
#   sub_69A754(easing, t)          — when easing variant type != 0
#   sub_698454(&rot_out, rot, ...) — when rotation variant type != 0
#   plus transitive bezier arm targets above
POSITION_INTERP_TARGETS = [
    0x69A4D4,   # sub_69A4D4 — entry
    0x69A754,   # bezier (easing branch)
    0x698454,   # rotation inner
    0x56C694,   # transitive
    0x6695BC,   # transitive
]

# --- geometry_hit_test: Player_hitTest (leaf, plain-C) -------------------
# The adapter passes a single HitData pointer + two doubles (point x/y)
# and reads the int return. No interesting callees — hooking the entry
# is enough to catch signature drift across port/libkrkr2 rebuilds.
GEOMETRY_HIT_TEST_TARGETS = [
    0x690DF0,   # Player_hitTest
]

# --- local_transform: sub_699940 (leaf, builds 2x2) ----------------------
# Takes (Layer*, Affine*), returns void, no libm calls in the hot path —
# libm sin/cos only fire on the rotate-transform branch and the adapter
# already rounds to bit-exact doubles on the Python A×L step.
LOCAL_TRANSFORM_TARGETS = [
    0x699940,   # sub_699940
]

# --- psbfile_load: PSBFile raw/MDF load and seed filter paths -------------
# The adapter supplies an existing PSB\0 or mdf\0 input. sub_598268 optionally
# unwraps MDF and hands the raw allocation to sub_598708; the adapter then
# invokes sub_598960(strict=true) to verify the reconstructed header pointers.
PSBFILE_LOAD_TARGETS = [
    0x598268,   # PSBFile.load(tTJSVariant const&)
    0x598538,   # load storage, read stream, and unwrap MDF
    0x598708,   # adopt raw PSB allocation into intrusive owner
    0x598960,   # refresh header pointers and optionally validate offsets
    0x6863CC,   # Emote PSB seed-filter call operator
]

# --- psbfile_integer: natural tag-0x04..0x09 raw + TJS conversion boundary --
# The public script constructs PSBFile(path), walks Dictionary/Array adaptors,
# and leaves the value in a real tTJSVariant.  The adapter then invokes the two
# raw getters on the same pinned node to separate 64-bit TJS Integer semantics
# from GetInt's W32-observable boundary.
PSBFILE_INTEGER_TARGETS = PSBFILE_LOAD_TARGETS + [
    0x59B14C,   # NCB PSBFile factory FuncCall wrapper
    0x5980F4,   # PSBFile NCB factory
    0x5981F8,   # PSBFile::GetRootDispatch
    0x59B28C,   # NCB root property getter wrapper
    0x59B48C,   # NCB root property native invoker
    0x597854,   # PSBValueDispatch::PropGet (Dictionary)
    0x5976C4,   # PSBValueDispatch::PropGetByNum (Array)
    0x59673C,   # PSBValueDispatch::CreateVariant_guess
    0xA0FF60,   # tTJSVariant::operator=(tjs_int64)
    0x599438,   # PSBRawNode::GetInt
    0x5992E8,   # PSBRawNode::GetDouble
]

# --- psbfile_real: float32/float64 CreateVariant + raw GetDouble -----------
# Three immutable natural nodes cover the zero token, float32 widening and
# float64 payload paths.  The public and raw results are compared bit-for-bit.
PSBFILE_REAL_TARGETS = PSBFILE_LOAD_TARGETS + [
    0x59B14C,   # NCB PSBFile factory FuncCall wrapper
    0x5980F4,   # PSBFile NCB factory
    0x5981F8,   # PSBFile::GetRootDispatch
    0x59B28C,   # NCB root property getter wrapper
    0x59B48C,   # NCB root property native invoker
    0x597854,   # PSBValueDispatch::PropGet (Dictionary)
    0x5976C4,   # PSBValueDispatch::PropGetByNum (Array)
    0x59673C,   # PSBValueDispatch::CreateVariant_guess
    0xA0FF94,   # tTJSVariant Real assignment
    0x5992E8,   # PSBRawNode::GetDouble
]

# --- psbfile_string: copied TJS String + borrowed raw UTF-8 pointer --------
# The selected tag-0x15/0x16 nodes both contain non-ASCII UTF-8.  The public
# Variant must outlive its PSBFile owner while GetString returns the exact
# owner-relative, NUL-terminated pointer.
PSBFILE_STRING_TARGETS = PSBFILE_LOAD_TARGETS + [
    0x59B14C,   # NCB PSBFile factory FuncCall wrapper
    0x5980F4,   # PSBFile NCB factory
    0x5981F8,   # PSBFile::GetRootDispatch
    0x59B28C,   # NCB root property getter wrapper
    0x59B48C,   # NCB root property native invoker
    0x597854,   # PSBValueDispatch::PropGet (Dictionary)
    0x5976C4,   # PSBValueDispatch::PropGetByNum (Array)
    0x59673C,   # PSBValueDispatch::CreateVariant_guess
    0xA0FEB4,   # tTJSVariant UTF-8 String assignment
    0x598B58,   # PSBRawNode::GetString
    0xA0F778,   # tTJSVariant destructor
    0xA0F790,   # tTJSVariant ReleaseContent
]

# --- psbfile_resource: copied Octet + borrowed raw Resource boundary --------
# The public path copies one natural tag-0x19 Resource into a refcounted Octet,
# releases the PSBFile owner, and verifies the Octet stays alive.  The raw path
# independently returns a borrowed chunk pointer and uint32 size.
PSBFILE_RESOURCE_TARGETS = PSBFILE_LOAD_TARGETS + [
    0x59B14C,   # NCB PSBFile factory FuncCall wrapper
    0x5980F4,   # PSBFile NCB factory
    0x5981F8,   # PSBFile::GetRootDispatch
    0x59B28C,   # NCB root property getter wrapper
    0x59B48C,   # NCB root property native invoker
    0x597854,   # PSBValueDispatch::PropGet (Dictionary)
    0x59673C,   # PSBValueDispatch::CreateVariant_guess
    0x5996E4,   # PSBRawNode::GetResource
    0xA0E0F4,   # allocate/copy tTJSVariantOctet
    0xA0FB64,   # tTJSVariant::CopyRef
    0xA0F778,   # tTJSVariant destructor
    0xA0F790,   # tTJSVariant ReleaseContent
    0x598B3C,   # PSBRawOwner destructor
]

# --- psbfile_shape: Null + collection enumeration/owner lifetimes ----------
# Natural tag-0x01/0x20/0x21 nodes cover Void conversion, raw category,
# raw GetRoot/Transfer holder ownership through hidden-sret X8,
# raw Dictionary strict/non-strict lookup, both packed-helper misses,
# destination overwrite, self/out aliasing, validity, contains cleanup, and
# ordered gnustl vector<string>/COW name decoding plus target-owned teardown,
# native-instance secondary-base borrowing, primary/secondary native lifecycle
# duplicate slots, all unsupported primary dispatch slots, exact collection
# class mapping, ordered value/no-value enumeration through a real TJS closure,
# closure Object/ObjThis double references, and the final PSBRawOwner reference.
PSBFILE_SHAPE_TARGETS = PSBFILE_LOAD_TARGETS + [
    0x59B14C,   # NCB PSBFile factory FuncCall wrapper
    0x5980F4,   # PSBFile NCB factory
    0x5981F8,   # PSBFile::GetRootDispatch
    0x598A3C,   # PSBFile::GetRoot hidden-sret holder retain
    0x598A64,   # PSBFile::Transfer hidden-sret ownership move
    0x598C58,   # PSBRawNode strict Dictionary lookup hidden-sret
    0x598D58,   # PSBRawNode non-strict in/out Dictionary lookup
    0x598E44,   # PSBRawNode owner/node validity predicate
    0x5995D8,   # PSBRawNode ContainsDictionaryKey temporary lifecycle
    0x59641C,   # packed names trie lookup
    0x59659C,   # packed Dictionary offset lookup
    0x598E64,   # ordered gnustl vector<string> Dictionary keys hidden-sret
    0x599174,   # std::vector<std::string>::reserve exact-count allocation
    0x597B1C,   # packed name decode into reusable COW std::string
    0x918690,   # target-owned std::vector<std::string> destructor
    0x59B28C,   # NCB root property getter wrapper
    0x59B48C,   # NCB root property native invoker
    0x596D90,   # PSBValueDispatch::NativeInstanceSupport
    0x597A30,   # primary PSBValueDispatch::Construct
    0x597A38,   # secondary iTJSNativeInstance::Construct duplicate
    0x596F38,   # primary native Invalidate no-op
    0x596F3C,   # secondary native Invalidate duplicate
    0x597A28,   # primary native Destruct no-op
    0x597A2C,   # secondary native Destruct duplicate
    0x597A20,   # unsupported FuncCall
    0x597A18,   # unsupported FuncCallByNum
    0x5976BC,   # unsupported PropSet
    0x5976B4,   # unsupported PropSetByNum
    0x5975D8,   # unsupported GetCountByNum
    0x5975D0,   # unsupported PropSetByVS
    0x596F48,   # unsupported DeleteMember
    0x596F40,   # unsupported DeleteMemberByNum
    0x596F04,   # unsupported InvalidateByNum
    0x596EE8,   # unsupported IsValidByNum
    0x596EE0,   # unsupported CreateNew
    0x596ED8,   # unsupported CreateNewByNum
    0x596ED0,   # unsupported Reserved1
    0x596E1C,   # unsupported IsInstanceOfByNum
    0x596E14,   # unsupported Operation
    0x596E0C,   # unsupported OperationByNum
    0x596D88,   # unsupported ClassInstanceInfo
    0x596D80,   # unsupported Reserved2
    0x596D78,   # unsupported Reserved3
    0x596E24,   # PSBValueDispatch::IsInstanceOf
    0x596F50,   # PSBValueDispatch::EnumMembers
    0x597854,   # PSBValueDispatch::PropGet (Dictionary)
    0x5976C4,   # PSBValueDispatch::PropGetByNum (Array)
    0x5975E0,   # PSBValueDispatch::GetCount
    0x596F0C,   # PSBValueDispatch dispatch Invalidate
    0x596EF0,   # PSBValueDispatch::IsValid
    0x59673C,   # PSBValueDispatch::CreateVariant_guess
    0x597AC0,   # PSBValueDispatch::AddRef
    0x597A40,   # PSBValueDispatch::Release
    0x599554,   # PSBRawNode::GetTypeCategory
    # Keep the high-frequency generic tTJSVariant CopyRef/destructor helpers
    # out of the shape-only target set. PSBValueDispatch AddRef/Release and
    # PSBRawOwner destruction are the collection-specific lifetime boundary;
    # String/Resource trace the generic Variant helpers independently. A
    # combined multi-family run may still include them through that union.
    0x598B3C,   # PSBRawOwner destructor
]

# --- psbfile_media: interface ABI + replacement/borrowed stream lifecycle ---
# The interface case fixes all simple media vslots and both destructor forms.
# The storage cases extend the load targets because EnsureContainer enters
# 0x598538 for each container switch.  The adapter observes the old stream's
# fields but never dereferences its borrowed Block after replacement.
PSBFILE_MEDIA_TARGETS = PSBFILE_LOAD_TARGETS + [
    0x59849C,   # PSBMedia process-lifetime singleton pre-register callback
    0x5997F0,   # PSBMedia complete destructor
    0x599830,   # PSBMedia deleting destructor
    0x599878,   # PSBMedia non-atomic AddRef
    0x599888,   # PSBMedia Release exact-one delete gate
    0x5998A8,   # PSBMedia GetName -> UTF-16 "psb"
    0x5998BC,   # PSBMedia NormalizeDomainName no-op
    0x5998C0,   # PSBMedia NormalizePathName no-op
    0x5998C4,   # PSBMedia::CheckExistentStorage
    0x59993C,   # PSBMedia::Open
    0x5999F4,   # PSBMedia::GetListAt
    0x599DD8,   # PSBMedia GetLocallyAccessibleName clears ttstr
    0x599E04,   # PSBMedia::EnsureContainer
    0x59A0B4,   # PSBMedia::GetResourceData
    0x59A330,   # PSBFile NCB adaptor creation
    0x59A4B0,   # PSBMedia::Resolve
    0x8F7C74,   # tTVPMemoryStream borrowed-block ctor
    0x8F7D68,   # tTVPMemoryStream deleting dtor
]


ADDR_NAMES = {
    0x69A754: "sub_69A754",
    0x69A4D4: "sub_69A4D4",
    0x698454: "sub_698454",
    0x56C694: "sub_56C694",
    0x6695BC: "sub_6695BC",
    0x690DF0: "Player_hitTest",
    0x699940: "sub_699940",
    0x598268: "PSBFile_loadVariant",
    0x598538: "PSBFile_loadStorage",
    0x598708: "PSBFile_adoptRaw",
    0x598960: "PSBRawOwner_refresh",
    0x6863CC: "EmotePSBDecrypt_call",
    0x59B14C: "PSBFile_factoryFuncCall",
    0x5980F4: "PSBFile_factory",
    0x5981F8: "PSBFile_getRootDispatch",
    0x598A3C: "PSBFile_getRootRaw",
    0x598A64: "PSBFile_transferRaw",
    0x598C58: "PSBRawNode_getDictionaryValueStrict",
    0x598D58: "PSBRawNode_getDictionaryValue",
    0x598E44: "PSBRawNode_isValid",
    0x5995D8: "PSBRawNode_containsDictionaryKey",
    0x59641C: "PSB_findNameIndex",
    0x59659C: "PSB_findDictionaryValueOffset",
    0x598E64: "PSBRawNode_getDictionaryKeys",
    0x599174: "std_vector_string_reserve",
    0x597B1C: "PSB_decodeName",
    0x918690: "std_vector_string_dtor",
    0x59B28C: "PSBFile_rootNcbGetter",
    0x59B48C: "PSBFile_rootNativeInvoker",
    0x596D90: "PSBValueDispatch_nativeInstanceSupport",
    0x597A30: "PSBValueDispatch_constructPrimary",
    0x597A38: "PSBValueDispatch_constructSecondary",
    0x596F38: "PSBValueDispatch_nativeInvalidatePrimary",
    0x596F3C: "PSBValueDispatch_nativeInvalidateSecondary",
    0x597A28: "PSBValueDispatch_nativeDestructPrimary",
    0x597A2C: "PSBValueDispatch_nativeDestructSecondary",
    0x597A20: "PSBValueDispatch_funcCallUnsupported",
    0x597A18: "PSBValueDispatch_funcCallByNumUnsupported",
    0x5976BC: "PSBValueDispatch_propSetUnsupported",
    0x5976B4: "PSBValueDispatch_propSetByNumUnsupported",
    0x5975D8: "PSBValueDispatch_getCountByNumUnsupported",
    0x5975D0: "PSBValueDispatch_propSetByVsUnsupported",
    0x596F48: "PSBValueDispatch_deleteMemberUnsupported",
    0x596F40: "PSBValueDispatch_deleteMemberByNumUnsupported",
    0x596F04: "PSBValueDispatch_invalidateByNumUnsupported",
    0x596EE8: "PSBValueDispatch_isValidByNumUnsupported",
    0x596EE0: "PSBValueDispatch_createNewUnsupported",
    0x596ED8: "PSBValueDispatch_createNewByNumUnsupported",
    0x596ED0: "PSBValueDispatch_reserved1Unsupported",
    0x596E1C: "PSBValueDispatch_isInstanceOfByNumUnsupported",
    0x596E14: "PSBValueDispatch_operationUnsupported",
    0x596E0C: "PSBValueDispatch_operationByNumUnsupported",
    0x596D88: "PSBValueDispatch_classInstanceInfoUnsupported",
    0x596D80: "PSBValueDispatch_reserved2Unsupported",
    0x596D78: "PSBValueDispatch_reserved3Unsupported",
    0x596E24: "PSBValueDispatch_isInstanceOf",
    0x596F50: "PSBValueDispatch_enumMembers",
    0x597854: "PSBValueDispatch_propGet",
    0x5976C4: "PSBValueDispatch_propGetByNum",
    0x5975E0: "PSBValueDispatch_getCount",
    0x596F0C: "PSBValueDispatch_invalidate",
    0x596EF0: "PSBValueDispatch_isValid",
    0x59673C: "PSBValueDispatch_createVariant",
    0xA0FF60: "tTJSVariant_assignInt64",
    0xA0FF94: "tTJSVariant_assignReal",
    0xA0FEB4: "tTJSVariant_assignUtf8String",
    0x599438: "PSBRawNode_getInt",
    0x5992E8: "PSBRawNode_getDouble",
    0x598B58: "PSBRawNode_getString",
    0x5996E4: "PSBRawNode_getResource",
    0x597AC0: "PSBValueDispatch_addRef",
    0x597A40: "PSBValueDispatch_release",
    0x599554: "PSBRawNode_getTypeCategory",
    0xA0E0F4: "TJS_allocVariantOctet",
    0xA0FB64: "tTJSVariant_copyRef",
    0xA0F778: "tTJSVariant_dtor",
    0xA0F790: "tTJSVariant_releaseContent",
    0x598B3C: "PSBRawOwner_dtor",
    0x59849C: "PSBMedia_register",
    0x5997F0: "PSBMedia_completeDtor",
    0x599830: "PSBMedia_deletingDtor",
    0x599878: "PSBMedia_addRef",
    0x599888: "PSBMedia_release",
    0x5998A8: "PSBMedia_getName",
    0x5998BC: "PSBMedia_normalizeDomainName",
    0x5998C0: "PSBMedia_normalizePathName",
    0x5998C4: "PSBMedia_checkStorage",
    0x59993C: "PSBMedia_open",
    0x5999F4: "PSBMedia_getListAt",
    0x599DD8: "PSBMedia_getLocallyAccessibleName",
    0x599E04: "PSBMedia_ensureContainer",
    0x59A0B4: "PSBMedia_getResourceData",
    0x59A330: "PSBFile_createAdaptor",
    0x59A4B0: "PSBMedia_resolve",
    0x8F7C74: "tTVPMemoryStream_ctorBorrowed",
    0x8F7D68: "tTVPMemoryStream_deletingDtor",
}

# (n_int_args, n_double_args) per target — AAPCS64 arg count cap of 8
# each. Everything beyond these positions is register garbage left by
# the caller's FP/int math and must be masked when normalising; otherwise
# the trace diff fires on meaningless bits.
#
# Signatures (from IDA):
#   sub_69A754(tTJSVariant *curve, double t)
#   sub_69A4D4(tTJSVariant *easing, double *src, double *dst, double *out,
#              int coord_mode, tTJSVariant *rotation, double t, double _)
#   sub_698454(double *out_xy, tTJSVariant *rotation, double t)
#   sub_56C694(tTJSArray *arr)                        → length
#   sub_6695BC(tTJSArray *arr, int idx, tTJSVariant *out, int flag)
ARG_COUNTS: dict[int, tuple[int, int]] = {
    0x69A754: (1, 1),
    0x69A4D4: (6, 2),
    0x698454: (2, 1),
    0x56C694: (1, 0),
    0x6695BC: (4, 0),
    # Player_hitTest(HitData *hd, double px, double py) → int
    0x690DF0: (1, 2),
    # sub_699940(Layer *node, Affine *ctx) → void
    0x699940: (2, 0),
    # PSBFile_loadVariant(PSBFile *this, tTJSVariant const *value) → int
    0x598268: (2, 0),
    # PSBFile_loadStorage(PSBFile *, ttstr const&, function const&) → bool
    0x598538: (3, 0),
    # PSBFile_adoptRaw(PSBFile *this, uint8_t *data, size_t, function *)
    0x598708: (4, 0),
    # PSBRawOwner_refresh(PSBRawOwner *this, bool strict) → bool
    0x598960: (2, 0),
    # EmotePSBDecrypt_call(seed closure slot, PSBRawOwner *)
    0x6863CC: (2, 0),
    # NCB iTJSDispatch2::FuncCall(..., result, argc, argv, objthis)
    0x59B14C: (8, 0),
    # PSBFileFactory(PSBFile **result, int argc, tTJSVariant **argv, objthis)
    0x5980F4: (4, 0),
    # PSBFile::GetRootDispatch(this)
    0x5981F8: (1, 0),
    # Non-trivial returns use hidden X8; source/self remains in X0.
    0x598A3C: (1, 0),
    0x598A64: (1, 0),
    # Raw Dictionary: hidden-sret strict result lives in X8; the in/out
    # non-strict result is the third ordinary argument.
    0x598C58: (2, 0),
    0x598D58: (3, 0),
    0x598E44: (1, 0),
    0x5995D8: (2, 0),
    0x59641C: (3, 0),
    0x59659C: (3, 0),
    # Hidden-sret result lives in X8; vector internals are ordinary calls.
    0x598E64: (1, 0),
    0x599174: (2, 0),
    0x597B1C: (3, 0),
    0x918690: (1, 0),
    # NCB root property wrapper/native invoker
    0x59B28C: (6, 0),
    0x59B48C: (3, 0),
    # iTJSDispatch2 native/type/enumeration/property accessors
    0x596D90: (4, 0),
    # Primary/secondary iTJSNativeInstance lifecycle duplicate entries
    0x597A30: (4, 0),
    0x597A38: (4, 0),
    0x596F38: (1, 0),
    0x596F3C: (1, 0),
    0x597A28: (1, 0),
    0x597A2C: (1, 0),
    # Unsupported primary iTJSDispatch2 slots: every body returns -1002.
    0x597A20: (8, 0),
    0x597A18: (7, 0),
    0x5976BC: (6, 0),
    0x5976B4: (5, 0),
    0x5975D8: (4, 0),
    0x5975D0: (5, 0),
    0x596F48: (5, 0),
    0x596F40: (4, 0),
    0x596F04: (4, 0),
    0x596EE8: (4, 0),
    0x596EE0: (8, 0),
    0x596ED8: (7, 0),
    0x596ED0: (1, 0),
    0x596E1C: (5, 0),
    0x596E14: (7, 0),
    0x596E0C: (6, 0),
    0x596D88: (4, 0),
    0x596D80: (1, 0),
    0x596D78: (1, 0),
    0x596E24: (6, 0),
    0x596F50: (4, 0),
    0x597854: (6, 0),
    0x5976C4: (5, 0),
    0x5975E0: (5, 0),
    0x596F0C: (5, 0),
    0x596EF0: (5, 0),
    # CreateVariant_guess(this, result, rawNode), Variant::operator=(int64)
    0x59673C: (3, 0),
    0xA0FF60: (2, 0),
    0xA0FF94: (1, 1),
    0xA0FEB4: (2, 0),
    # PSBRawNode scalar getters
    0x599438: (1, 0),
    0x5992E8: (1, 0),
    0x598B58: (1, 0),
    # PSBRawNode::GetResource(this, uint32_t *size), Octet/Variant lifecycle
    0x5996E4: (2, 0),
    0x597AC0: (1, 0),
    0x597A40: (1, 0),
    0x599554: (1, 0),
    0xA0E0F4: (2, 0),
    0xA0FB64: (2, 0),
    0xA0F778: (1, 0),
    0xA0F790: (1, 0),
    0x598B3C: (1, 0),
    # PSBMedia process-lifetime pre-register callback (source-level void)
    0x59849C: (0, 0),
    # PSBMedia destructor/refcount/name/normalization interface slots.
    0x5997F0: (1, 0),
    0x599830: (1, 0),
    0x599878: (1, 0),
    0x599888: (1, 0),
    0x5998A8: (2, 0),
    0x5998BC: (2, 0),
    0x5998C0: (2, 0),
    # PSBMedia::CheckExistentStorage(this, ttstr const&)
    0x5998C4: (2, 0),
    # PSBMedia::Open(this, ttstr const&, flags); flags is source-level ABI
    # even though the optimized body does not consume x2.
    0x59993C: (3, 0),
    # PSBMedia::GetListAt(this, ttstr const&, iTVPStorageLister *)
    0x5999F4: (3, 0),
    # PSBMedia::GetLocallyAccessibleName(this, ttstr&)
    0x599DD8: (2, 0),
    # PSBMedia::EnsureContainer(this, ttstr const&)
    0x599E04: (2, 0),
    # PSBMedia::GetResourceData(this, ttstr const&, uint32_t *size)
    0x59A0B4: (3, 0),
    # CreateAdaptor(PSBFile **holder, bool takeOwnership, bool throwOnError)
    0x59A330: (3, 0),
    # PSBMedia::Resolve(this, ttstr const&, PSBRawNode *out)
    0x59A4B0: (3, 0),
    # tTVPMemoryStream(this, const void *block, uint32_t size)
    0x8F7C74: (3, 0),
    # deleting ~tTVPMemoryStream(this): dtor body + operator delete(this)
    0x8F7D68: (1, 0),
}

# Return kind per target: "int", "double", "void". Drives exit-event
# canonicalisation — for "double"-returning fns we keep d0 and drop x0
# (AAPCS64 puts doubles in d0, leaving x0 as scratch), etc.
RETURN_KINDS: dict[int, str] = {
    0x69A754: "double",
    0x69A4D4: "void",
    0x698454: "void",
    0x56C694: "int",
    0x6695BC: "double",
    0x690DF0: "int",
    0x699940: "void",
    0x598268: "int",
    0x598538: "int",
    0x598708: "int",
    0x598960: "int",
    0x6863CC: "int",
    0x59B14C: "int",
    0x5980F4: "int",
    0x5981F8: "int",
    # Result bytes are written through hidden X8; X0 is not a return value.
    0x598A3C: "void",
    0x598A64: "void",
    0x598C58: "void",
    0x598D58: "int",
    0x598E44: "int",
    0x5995D8: "int",
    0x59641C: "int",
    0x59659C: "int",
    0x598E64: "void",
    0x599174: "void",
    0x597B1C: "void",
    0x918690: "void",
    0x59B28C: "int",
    0x59B48C: "int",
    0x596D90: "int",
    0x597A30: "int",
    0x597A38: "int",
    0x596F38: "void",
    0x596F3C: "void",
    0x597A28: "void",
    0x597A2C: "void",
    0x597A20: "int",
    0x597A18: "int",
    0x5976BC: "int",
    0x5976B4: "int",
    0x5975D8: "int",
    0x5975D0: "int",
    0x596F48: "int",
    0x596F40: "int",
    0x596F04: "int",
    0x596EE8: "int",
    0x596EE0: "int",
    0x596ED8: "int",
    0x596ED0: "int",
    0x596E1C: "int",
    0x596E14: "int",
    0x596E0C: "int",
    0x596D88: "int",
    0x596D80: "int",
    0x596D78: "int",
    0x596E24: "int",
    0x596F50: "int",
    0x597854: "int",
    0x5976C4: "int",
    0x5975E0: "int",
    0x596F0C: "int",
    0x596EF0: "int",
    0x59673C: "int",
    0xA0FF60: "int",
    0xA0FF94: "int",
    0xA0FEB4: "int",
    0x599438: "int",
    0x5992E8: "double",
    0x598B58: "int",
    0x5996E4: "int",
    0x597AC0: "int",
    0x597A40: "int",
    0x599554: "int",
    0xA0E0F4: "int",
    0xA0FB64: "void",
    0xA0F778: "void",
    0xA0F790: "void",
    0x598B3C: "void",
    0x59849C: "void",
    0x5997F0: "void",
    0x599830: "void",
    0x599878: "void",
    0x599888: "void",
    0x5998A8: "void",
    0x5998BC: "void",
    0x5998C0: "void",
    0x5998C4: "int",
    0x59993C: "int",
    0x5999F4: "void",
    0x599DD8: "void",
    0x599E04: "int",
    0x59A0B4: "int",
    0x59A330: "int",
    0x59A4B0: "int",
    0x8F7C74: "void",
    0x8F7D68: "void",
}


def return_kind(offset: int) -> str:
    return RETURN_KINDS.get(offset, "int")


def addr_name(offset: int) -> str:
    return ADDR_NAMES.get(offset, f"sub_{offset:X}")


def arg_counts(offset: int) -> tuple[int, int]:
    """(n_int, n_double) for the target, or (8, 8) if unknown.

    An unknown target has full 8/8 so we keep every register — catches
    misconfigured targets at golden-record time rather than silently
    dropping data.
    """
    return ARG_COUNTS.get(offset, (8, 8))
