# motionplayer ttstr hash Hint-cache wrapper / core Make layer four-binary audit

## 1. Scope and conclusion

This slice re-audits the `ttstr` hash used by motionplayer's unordered
containers after finding one remaining source comment that still cited the old
single-`libkrkr2.so` analysis.  It resolves an apparent conflict between two
earlier observations:

- the KiriKiri core `tTJSHashFunc<ttstr>::Make` helper does not access the
  `tTJSVariantString::Hint` word;
- motionplayer container lookups do visibly read and write that Hint word.

Both observations are correct because the references have two distinct layers:

1. the core Make operation is a pure 32-bit UTF-16 payload hash;
2. each motionplayer unordered-container wrapper handles the shared Hint cache,
   invokes/inlines the pure operation only for a zero Hint, publishes the
   result, and then performs bucket lookup.

All four references agree on the observable state machine, including the
otherwise easy-to-conflate distinction between a null-backed `ttstr` and a
non-null allocated empty string.  The current portable implementation already
matches that behavior.  The source change therefore removes stale provenance,
documents the two layers precisely, and adds missing copied-alias coverage; it
does not alter production hash code.

## 2. Four-image mapping

The representative `unordered_map<ttstr,double>::operator[]` specialization is
named `LabelValueMap_getOrInsertMapped_guess` in every recovery database:

| Reference | operator[] | code xrefs | core Make form |
|---|---:|---:|---|
| Android arm64-v8a | `0x683D24` | 32 | inlined in wrapper |
| Android armeabi-v7a | `0x56559C` | 15 | call to `0x497AFA` |
| iOS arm64 | `0x10010BD28` | 15 | call to `0x100039AEC` |
| iOS armv7 | `0x1096A4` | 15 | call to `0x3798C` |

The non-inlined core helper has 155 direct xrefs in Android armv7 and 160 in
each iOS image.  Those broad core xref sets are compatible with a generic
KiriKiri string hash.  The representative operator[] functions are the
motionplayer-specific evidence for cache ownership and map-node behavior.

The Android arm64 compiler inlines the same arithmetic into this specialization;
absence of a call instruction there is not a behavioral difference.

## 3. Recovered two-layer state machine

At source level the wrapper behavior is:

```cpp
size_t hash_ttstr_for_container(const ttstr &key) {
    uint32_t *hint = key.GetHint();
    if (hint == nullptr)
        return 0;
    if (*hint != 0)
        return *hint;

    uint32_t hash = core_hash_utf16(key.c_str());
    *hint = hash;
    return hash;
}
```

The significant boundaries are:

- the `ttstr` object contains/forwards a shared backing pointer;
- `GetHint()` identifies a 32-bit word in that backing allocation, at `+0x44`
  on LP64 and `+0x3C` on ILP32;
- a null backing pointer is detected before the core hash and returns zero;
- every nonzero Hint is accepted exactly, even if it was externally seeded and
  is unrelated to the payload;
- only zero means uncomputed;
- after computation, the wrapper stores the result before bucket lookup;
- copied `ttstr` aliases share the same backing and therefore immediately
  observe the cached word;
- the 32-bit value is zero-extended when the function's `size_t` result is
  64-bit;
- there is no atomic operation, lock, or second validation around Hint access.

The cache is therefore part of observable string-backing state, not a private
field of one map or one `ttstr` value object.

## 4. Core UTF-16 payload algorithm

The pure layer consumes UTF-16 code units and keeps all arithmetic in 32 bits:

```text
acc = 0
c = *p                         // when p is non-null
if c != 0:
    advance p
    do:
        mixed = acc + c
        c = *p++
        acc = (1025 * mixed) XOR ((1025 * mixed) >> 6)
    while c != 0
    acc = 9 * acc

h = 32769 * (acc XOR (acc >> 11))
if h == 0:
    h = UINT32_MAX
return h
```

The `UINT32_MAX` substitution preserves zero as the wrapper's uncomputed
sentinel.  A raw null payload pointer and a raw empty payload both leave `acc`
at zero and therefore return `UINT32_MAX` from this pure layer.

This does not contradict a null-backed `ttstr` hashing to zero: the container
wrapper recognizes that object state and bypasses the pure layer entirely.

## 5. Null, empty, cached, and alias behavior

| key state | backing / Hint | core call | returned hash | Hint write |
|---|---|---|---:|---|
| null/default `ttstr` | no backing, no Hint pointer | no | `0` | none |
| allocated empty string, initial Hint 0 | backing exists | yes | `UINT32_MAX` | `UINT32_MAX` |
| nonempty string, initial Hint 0 | backing exists | yes | computed nonzero/sentinel | computed value |
| any string, nonzero Hint | backing exists | no | exact cached word | none |
| raw null `tjs_char *` overload | no `ttstr` wrapper | pure helper | `UINT32_MAX` | n/a |
| raw empty payload overload | no `ttstr` wrapper | pure helper | `UINT32_MAX` | n/a |

An allocated empty string is intentionally not collapsed to the default null
`ttstr` for hashing.  The two states select different buckets (`UINT32_MAX`
versus `0`) even though ordinary string equality may treat their textual
payloads as empty in other contexts.

For aliases, computing through either copy updates the common Hint word.
Likewise, writing an arbitrary nonzero word through one alias causes the other
alias to return that exact word without rehashing the payload.

## 6. Bucket lookup, hit, and miss flow

After acquiring the wrapper hash, all four specializations perform the same
logical sequence:

1. derive/select the bucket;
2. walk nodes in that bucket;
3. compare the node's cached hash before string equality;
4. on hit, return the address of the existing mapped `double`;
5. on miss, allocate a node, CopyRef the `ttstr` key, value-initialize the
   mapped `double` to positive zero, store the node hash, and insert/rehash;
6. return the mapped `double` address.

A hit does not copy the key, rewrite Hint, relink the node, or replace its
cached hash.  Consequently, mutating a shared Hint externally after insertion
can make a later lookup search a different bucket; the wrapper trusts the Hint
contract rather than defending against such mutation.

## 7. Concrete STL node layouts

The logical node contents agree, but Android's old libstdc++ and iOS libc++
place the cached hash differently:

| Reference ABI/STL | node size | next | padding / cached hash | key | mapped `double` | cached hash tail |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 old libstdc++ | `0x20` | `+0x00` | — | `+0x08` | `+0x10` | `+0x18` |
| Android armv7 old libstdc++ | `0x20` | `+0x00` | pad `+0x04` | `+0x08` | `+0x10` | `+0x18` |
| iOS arm64 libc++ | `0x20` | `+0x00` | hash `+0x08` | `+0x10` | `+0x18` | — |
| iOS armv7 libc++ | `0x14` | `+0x00` | hash `+0x04` | `+0x08` | `+0x0C` | — |

The platform-specific layout and rehash implementation must not be mistaken
for different motionplayer key/value semantics.  Conversely, matching only the
hash function cannot make Web STL iteration byte-for-byte identical to both
native STL families.

## 8. Portable-source and test alignment

`cpp/plugins/motionplayer/internal/ttstr_hash.h` now:

- removes the stale `.claude/agent-memory/...libstdcxx_spec.md` provenance;
- explicitly separates the Hint-pure core operation from the motionplayer
  container wrapper;
- documents null backing versus raw null/empty payload behavior;
- records exact nonzero-Hint acceptance, pre-lookup publication, alias sharing,
  and LP64 zero-extension;
- retains the existing production arithmetic and wrapper branches unchanged.

`tests/unit-tests/plugins/motionplayer-ttstr-hash-test.cpp` already covered the
raw algorithm, hand-traced `"a"` value, arbitrary cached word, null `ttstr`, and
allocated empty string.  This slice additionally covers:

- two copied `ttstr` objects exposing the same Hint address;
- computation through one alias becoming visible through the other;
- an externally seeded nonzero Hint being accepted through the other alias;
- the raw `const tjs_char *` null overload returning `UINT32_MAX`, in direct
  contrast to the null-backed `ttstr` result of zero.

## 9. Recovery-IDB writeback

All four databases were handled sequentially and saved/closed:

- 4 semantic `_guess` function renames;
- 4 function type applications;
- 40 comments covering layer ownership, backing/Hint offsets, hash arithmetic,
  null/empty behavior, pre-lookup publication, hit/miss flow, and node layout;
- 20 bookmarks.

The final Android arm64 arithmetic comment was re-read and corrected to record
the exact `>>6` per-code-unit mix and `>>11` final avalanche.  Final IDA session
audit returned zero open sessions.

## 10. Validation and products

- the dedicated `motionplayer-ttstr-hash-test.cpp` TU passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax-only compilation;
- Web rebuilt all 35 affected steps and linked successfully;
- Wasmtime rebuilt all 65 affected native/guest steps and linked successfully;
- both CTest invocations returned zero and reported no tests because the active
  trees have `ENABLE_TESTS=false`;
- Node successfully constructed `WebAssembly.Module` for both products;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- product sizes, SHA-256 hashes, and section sizes remain byte-identical to
  V220/V219, as expected from a comment-only production edit.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,793 B | `858A3677901252A11D37637BC3BE7423D1ACD9D019080E64E18276379CE49D55` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,934 B | `FC8847E666976A424C9BD1A4780E5124F071D114CB6373B1F6985AC350A22C08` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AB5` | `0x19E9A63` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 11. Limits

- this slice audits a representative `ttstr -> double` operator[] plus the
  shared core helper; it does not assert that every unordered-container
  specialization has identical node payload layout;
- allocation failure, key-CopyRef failure, and rehash unwind internals remain
  delegated to each STL implementation and were not promoted into new portable
  source claims here;
- the references do not synchronize Hint access; concurrent mutation of shared
  string/container state remains outside the supported container contract;
- matching the hash/Hint state machine does not make unordered iteration order
  identical across old libstdc++, libc++, and the Web toolchain's STL;
- this closes one stale-provenance/high-value container boundary and does not
  complete the full motionplayer recovery goal.
