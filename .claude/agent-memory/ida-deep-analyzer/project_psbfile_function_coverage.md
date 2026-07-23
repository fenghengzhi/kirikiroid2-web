---
name: psbfile-function-coverage
description: PSBFile.dll corrected 112-function coverage including the GetDictionaryKeys vector-growth slow path
metadata:
  type: project
---

# PSBFile.dll function coverage correction (2026-07-19)

The old `0x59641C..0x59AA84 = 90 functions` boundary was truncated:
`0x59AA84` starts, rather than ends, the PSBFile typed NCB registration tail.
Read-only IDAPython enumeration plus fresh decompile extends the plugin-related
range through `0x59B708`. Splitting the independent prologues at `0x59A8D8`,
`0x59A968`, and `0x59B14C` yields 111 business/NCB functions. A later
2026-07-23 instruction-level audit disproved the old terminal-boundary claim:
`GetDictionaryKeys@0x598E64` calls PLT `0x423250/0x42325C` at `0x598FFC`
when its vector is full, and that thunk reaches
`std::vector<std::string>::_M_emplace_back_aux<std::string &>@0x59B7E8`.
The related manifest therefore contains 112 functions and ends at the slow
path's exclusive end `0x59B9C8`. That address has an independent
`SUB SP,#0x30` prologue and is registered by `sub_42CFA0` as the literal
`PackinOne.dll` callback; its body loads `fstat.dll` and the remaining bundled
plugins. The IDB was corrected and saved with separate `0x59B7E8..0x59B9C8`
and `0x59B9C8..0x59BC2C` functions.

The 22-function typed NCB tail contains auto-register/unregister,
native-holder construction/three destructors,
finalize, member/global registration, duplicate-constructor state, root
property getter/RO setter wrappers, load method wrapper and first-argument
Variant-by-value conversion. They are generated locally by
`NCB_REGISTER_CLASS(PSBFile) + Factory/Property/Method`; no hand-written
simplification was found.

Full exhaustive address grouping and tail mapping:
`analysis/psbfile_function_coverage_2026-07-19.md`.

Fresh decompile also confirmed `0x596F50` as the actual
`PSBValueDispatch::EnumMembers` interface implementation; IDB name was corrected
from `PSBValueDispatch_EnumMembers_guess` to `PSBValueDispatch_EnumMembers` and
saved.

## 2026-07-23 deeper six-dimension audit

- `PropGet@0x597854` assigns array `count` through the int32 Variant helper
  `0xA0FF28` (`SXTW`), not an int64 zero-extension.
- `assign@0x59673C` String and `EnumMembers@0x596F50` dictionary names use the
  narrow-string Variant assignment helper `0xA0FEB4` directly; Enum flags are
  default-constructed, assigned int32 zero, and only then is callbackResult
  default-constructed.
- `assign@0x59673C` saves the incoming destination in X19 and returns that same
  address through `MOV X0,X19@0x596B88`; all four direct callers ignore it.
  Local code now returns its existing destination pointer instead of `void`.
  Pointer versus reference source spelling remains ABI-indistinguishable.
- A fresh static-only dispatch audit (no oracle runner and no Frida) fixed the
  exact tail semantics that Hex-Rays can hide. `GetCount@0x5975E0` writes its
  out pointer only for Array and returns -1002 for every known non-Array tag and
  for an unknown-tag helper-return. `PropGetByNum@0x5976C4` returns -1001 for a
  known non-Array tag; an out-of-range lookup clears result only without
  MEMBERMUSTEXIST. `PropGet@0x597854` rejoins the same flag gate after an
  unknown-tag helper-return, clearing result/returning 0 or returning -1001.
  None of the successful/clear sites guards a null result pointer. Local code
  matches these branches. IDB now has the semantic PSBValueDispatch layout,
  signatures/comments for all five audited functions, and the private helper
  name `PSBValueDispatch_assign_guess`; interface slot names remain exact.
- The A-group's three extra entries are secondary-base duplicates, not
  destructor wrappers: Construct `0x597A30/0x597A38`, native Invalidate
  `0x596F38/0x596F3C`, and native Destruct `0x597A28/0x597A2C`. In each pair
  the first is the main-vtable slot and the second is the secondary
  `iTJSNativeInstance` entry; local source correctly shares one method body.
  All six IDB entries now carry paired main/secondary comments and the IDB is
  saved.
- `PSBValueDispatch_assign_guess` complex temporary lifetimes are now closed
  through fresh decompile of `TJSAllocVariantOctet_guess@0xA0E0F4`,
  `tTJSVariant_CopyRef_guess@0xA0FB64`, and destructor wrapper
  `0xA0F778`/`tTJSVariant_ReleaseContent_guess@0xA0F790`. Non-null, non-empty
  Octet refcounts are 1 -> 2 -> 1. A new child dispatch goes 1 -> 3
  (temporary Object/ObjThis) -> 5 (result CopyRef) -> 3 (temporary destruction)
  -> 2 (construction-reference Release),
  leaving exactly result's two closure references. Local expression-temporary
  and explicit-Release ordering matches this chain.
- The existing `ezsave.pimg` unit test now destroys `PSBFile`, clears the root
  closure, exercises the child Array, clears the Array closure, and then
  exercises its child Dictionary. This is a local non-oracle guard for each
  dispatch independently retaining the raw owner; macOS Release passes
  577/577 assertions in 10 cases.
- Resource access in `0x59673C`, `0x596C70`, `0x5996E4`, and `0x59A0B4`
  constructs/loads offset-view metadata before length-view metadata, then reads
  the indexed length entry before the indexed offset entry. Only `0x59673C`
  needed an entry-order fix; the three helpers were already correct.
- `PSBFile::Load@0x598268` returns true at `0x5983B0` if the invalid-type throw
  helper unexpectedly returns; it must not continue into `AsOctet()`.
- `GetInt@0x599438` leaves full X0 on some wide-tag paths, but negative 8/16-bit
  paths return directly through `LDRSB/LDURSH W0` and float/double conversion
  uses `FCVTZS W0`. Eighteen consuming callers read W0 (four use signed
  `SCVTF D0,W0`) and two discard it. A signed-int64 interface would return the
  wrong positive value on negative narrow tags, so the interface is proven
  signed 32-bit; local `tjs_int` is correct and incidental X0 high bits are not
  part of the return value.
- `GetInt@0x599438` and `GetDouble@0x5992E8` have an outer tag dispatcher plus
  nested 32-bit integer, 64-bit integer, float, and double decoder shapes. Local
  code now shares four `_guess` decoders. Exact names/member identity and inline
  helper vs explicit nested source remain uncertain. The shared tag-0B decoder
  reads all 56 bits without bit-55 extension; GetInt's low-W32 machine path is
  wrapper truncation/optimization, not proof of a four-byte source decoder.
- `sub_598D58` has a confirmed aliased source/out caller at
  `sub_695DE8@0x696A84..0x696A90`: both X0 and X2 are `&v278` for the `"clip"`
  lookup. The try-get body has no self guard and performs release-old, reload
  source owner, AddRef, then child-node write. This does not prove the shape or
  self guard of any general raw-node assignment special member.
- A later full-function audit disproved the old "one raw-node scratch" model.
  Persistent `var_B0` is constructed at 0x6960D4, survives request probing,
  enumeration, second-pass decode, packed metadata and the self-aliased `clip`
  descent, then dies at 0x697380..0x6973A4. A separate per-record node `p` is
  constructed at 0x696F90 for `compress`/`pal` lookup and dies at
  0x69724C..0x697274. The record container is a contiguous value-vector with an
  embedded rect, rect-to-record backpointer, one sourceKey string and BGRA freed
  at the use site. Local PlayerResource now preserves both node lifetimes, the
  one-record-lagged palette gate and that value-vector/backpointer/free topology.
  Optimized code cannot uniquely decide whether the atlas helper was originally
  a source-level inline helper, so helper splitting is not evidence that the
  persistent-node lifetime remains open.
- `sub_695DE8@0x69612C..0x696154` releases the strict `icon` temporary owner
  after the second try-get but before branching on its saved result. Local code
  now uses an explicit scope plus bool so this temporary is not retained until
  helper exit.
- The repository's existing encrypted motion PSB is a valid second raw
  container. A local test now exercises `ezsave -> motion -> ezsave`, successful
  `EnsureContainer@0x599E04` replacement, and the old stream's own metadata and
  destructor after replacement. Borrowed/non-retaining block ownership remains
  proven by the stream ctor/dtor decompile; the test does not read the dangling
  block.
  The former claim that no second loadable container existed was false and has
  been corrected in the analysis. This remains local lifecycle validation, not
  an Android runtime oracle.
- The two committed media fixtures still cannot reach dictionary listing:
  ezsave's root has only Resource/Integer/Array children, the unfiltered motion
  root is Resource tag 0x1A, Resolve cannot return the root, and every path
  segment requires a Dictionary. This narrow fixture statement must not be used
  as the current overall reachability status: the restored ignored
  `reference/autoskip.psb` has a reachable `source/main/icon` Dictionary with
  the ordered keys `arrow/auto/skip`, so the dictionary branch is now closed by
  an existing natural asset.
