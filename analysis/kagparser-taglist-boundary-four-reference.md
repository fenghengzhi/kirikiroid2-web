# KAGParser taglist boundary

The default parser currently adds an enumerable `taglist` to returned tags.
Scripts that enumerate the dictionary can treat this extra key as an image
command. Four-reference decompilation distinguishes the default parser from
the explicitly loaded Ex and Exb implementations.

All addresses below belong to the named 1.3.9 binary. Each GetNextTag, helper,
registration callback and Exb factory/lifecycle function was independently
located and decompiled; full return paths and UTF-16 `taglist` bytes were checked.

| Reference binary | Base GetNextTag | Ex GetNextTag / attach helper | Exb GetNextTag / attach helper |
|---|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0xa1fda0 | 0x550e54 / 0x54f818 | 0x56231c / 0x569368 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x769f00 | 0x4ae2b4 / 0x4ad62c | 0x4b8dd8 / 0x4bc920 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x10003451c | 0x10009f1ec / 0x10009e2f8 | 0x100371ba4 / 0x1003766a8 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x328d4 | 0x9dd9c / 0x9cd30 | 0x374668 / 0x37970c |

Common control flow, established before changing the implementation:

```text
Base: parse -> AddRef(dictionary) -> return dictionary
Ex/Exb: parse -> dictionary.PropSetByVS(MEMBERENSURE, "taglist", names)
              -> AddRef(dictionary) -> return dictionary
Ex PreRegist: retain previous global class -> install Ex class
Exb PostRegist: acquire five module-held methods -> install Exb class
Exb instance: create private state; borrow methods; release only private state
```

The four Ex/Exb helpers use flags 512 (`MEMBERENSURE`), not a hidden member.
Ordinary tags, characters, line endings and interrupts use their respective
successful return paths. Macro recording, exclusion and tag conditions still
gate returns. Existing instances keep their implementation when plugins replace
the global class. Pointer/vtable offsets, EH and tail merging differ across ABI
and compiler; the output contract and method ownership agree across all four.

| Reference binary | Ex PreRegist | Exb PostRegist | Exb NI factory / Invalidate / destructor |
|---|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x558c94 | 0x55a9f8 | 0x5604a8 / 0x56a95c / 0x56aa68 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x4b29c8 | 0x4b3f20 | 0x4b7b3c / 0x4bd854 / 0x4bd8a4 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x1000a43a0 | 0x10036c2ec | 0x100370830 / 0x100377ad8 / 0x100377b44 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0xa31b8 | 0x36e76c | 0x372e1c / 0x37aaca / 0x37ab02 |

Exb acquires `Dictionary.clear`, `Dictionary.assign`, `Array.clear`,
`Array.assign`, `Array.push` in that order from script-global classes. Each
`AsObject` holds one reference; acquisition failure releases acquired pointers
in the same order and rethrows. Successful module registration has no matching
unregistration release; instances neither query nor AddRef/Release these methods.
The prior Android arm64 `sub_561F3C` constructor annotation was incorrect: it
belongs to a call-frame copy, not the Exb NI factory.

Local mapping: `ReturnsTagList` selects output by NI implementation; existing
return conditions and AddRef sites stay in place. Ex/Exb share existing TJS
bindings via separate factories. Two NCB modules install these classes at their
respective registration phases. Exb borrows the five module methods; its existing
name-append field aliases the cached `Array.push`. Base/Ex retain the current
per-instance acquisition paths. Internal ordered attributes, macro state and
serialization are not rewritten by this output-boundary fix.
