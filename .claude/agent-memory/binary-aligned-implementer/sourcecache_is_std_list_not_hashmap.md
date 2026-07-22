---
name: sourcecache-is-std-list-not-hashmap
description: SourceCache source container is std::list<Entry>; libstdc++ sentinel links are not a hand-written intrusive list, and ResourceManager.findSource owns the separate unordered_map
metadata:
  type: project
---

# SourceCache uses std::list<Entry>, not a custom intrusive list

2026-07-23 fresh decompile/disassembly corrected the earlier “intrusive list”
claim. `SourceCache_ctor@0x6A78F4` initializes the libstdc++ list sentinel at
`this+72/+80`; `_List_node_base::_M_hook/_M_unhook` and the node copier
`0x6EAC60` identify the source-level container as `std::list<Entry>`. The links
inside a libstdc++ list node are implementation detail, not fields authored in
`Entry`.

The semantic payload order established by `loadSource@0x6A7BA8` and the copier
is:

```cpp
struct Entry {
    tTJSVariant key;
    tTJSVariant layer;
    ttstr src;
    tjs_int blendMode;
    tjs_int colors[4];
    tjs_int byteWeight;
};
```

ARM64 offsets belong in analysis only. Do not pad the wasm class to reproduce
them.

Lookup identity is `(full strict tTJSVariant key, ttstr src, blendMode)`.
Colors are mutable payload, not part of the key. A same-color hit returns without
promotion. A color mismatch bakes the same Layer, then performs
`entries.push_front(*it); entries.erase(it);`, preserving the Entry copy and old
node destruction lifetimes. A miss trims, creates/bakes a Layer, accounts
`4*width*height`, and pushes a copy at the front.

`trim@0x6A6B08` is not a prefix cut: it scans newest-to-oldest and independently
keeps an entry when signed `(keptBytes + weight) <= uint32(limit*99/100)`, so the
survivors form a greedy subsequence. `clearCache@0x6A8438` dispatch-invalidates
each Layer, clears the list, and zeros current bytes.

`ResourceManager_findSource@0x6AAB3C` consumes the derived class's separate
`std::unordered_map` of mapped PSB records. That map must never be substituted
for SourceCache's list.
