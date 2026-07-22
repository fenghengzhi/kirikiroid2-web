---
name: psbfile-function-coverage
description: PSBFile.dll corrected 111-function boundary and exhaustive grouped manifest through the typed NCB tail
metadata:
  type: project
---

# PSBFile.dll function coverage correction (2026-07-19)

The old `0x59641C..0x59AA84 = 90 functions` boundary was truncated:
`0x59AA84` starts, rather than ends, the PSBFile typed NCB registration tail.
Read-only IDAPython enumeration plus fresh decompile extends the plugin-related
range through `0x59B708`. Splitting the independent prologues at `0x59A8D8`,
`0x59A968`, and `0x59B14C` yields 111 functions total. The next function at
`0x59B7E8` is a generic `std::vector<std::string>` instantiation with only
PLT/data xrefs and no call from the PSBFile tail.

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
- Resource access in `0x59673C`, `0x596C70`, `0x5996E4`, and `0x59A0B4`
  constructs/loads offset-view metadata before length-view metadata, then reads
  the indexed length entry before the indexed offset entry. Only `0x59673C`
  needed an entry-order fix; the three helpers were already correct.
- `PSBFile::Load@0x598268` returns true at `0x5983B0` if the invalid-type throw
  helper unexpectedly returns; it must not continue into `AsOctet()`.
- `GetInt@0x599438` materializes X0 for tags 09/0A/0C, while all 20 observed
  callers consume only W0. The source return type (`tjs_int` vs `tjs_int64`)
  remains unclosed; caller truncation is not proof of the callee signature.
- The repository's existing encrypted motion PSB is a valid second raw
  container. A local test now exercises `ezsave -> motion -> ezsave`, successful
  `EnsureContainer@0x599E04` replacement, and the old stream's own metadata and
  destructor after replacement. Borrowed/non-retaining block ownership remains
  proven by the stream ctor/dtor decompile; the test does not read the dangling
  block.
  The former claim that no second loadable container existed was false and has
  been corrected in the analysis. This remains local lifecycle validation, not
  an Android runtime oracle.
