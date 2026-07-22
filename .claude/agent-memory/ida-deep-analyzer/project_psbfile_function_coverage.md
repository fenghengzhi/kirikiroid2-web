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
