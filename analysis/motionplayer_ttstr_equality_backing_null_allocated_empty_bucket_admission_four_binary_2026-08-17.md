# motionplayer ttstr equality backing/null/allocated-empty bucket-admission four-binary audit

## 1. Scope and conclusion

V221 recovered motionplayer's two-layer `ttstr` hash and Hint-cache behavior.
This slice closes the matching equality half of unordered lookup: cached-node
hash admission, backing-pointer identity, null handling, Length gating, and
UTF-16 payload comparison.

All four references agree on this exact equality order:

```text
if lhs.backing == rhs.backing: true
if lhs.backing == null or rhs.backing == null: false
if lhs.backing.Length != rhs.backing.Length: false
return TJS_strcmp(lhs.payload, rhs.payload) == 0
```

The result is deliberately backing-aware rather than a pure projection through
`c_str()`:

- two default/null `ttstr` objects are equal;
- copied aliases are equal by the pointer fast path;
- default/null and allocated-empty are unequal;
- two independently allocated empty strings are equal after Length/payload
  comparison;
- equal nonempty text in separate backings is equal;
- Hint words are never read by equality.

This exactly complements V221: null-backed strings hash to `0`, allocated-empty
strings hash to `UINT32_MAX`, and independently allocated empty strings share
both equality and hash.  The current portable `ttstr_equal` already delegates
to the matching KiriKiri `ttstr::operator==`; production logic did not change.

## 2. Four-image mapping

| Reference | `LabelValueMap::operator[]` | bucket/equality placement | shared equality helper | helper code xrefs | UTF-16 content compare |
|---|---:|---|---:|---:|---:|
| Android arm64-v8a | `0x683D24` | old-libstdc++ lookup `0x683F4C`; equality inlined | inlined | lookup: 7 | `0x9B07D0` |
| Android armeabi-v7a | `0x56559C` | old-libstdc++ lookup `0x5656CC` | `0x497BA0` | 192 | `0x72A318` |
| iOS arm64 | `0x10010BD28` | libc++ bucket walk inline in operator[] | `0x10002E518` | 193 | `0x10036446C` |
| iOS armv7 | `0x1096A4` | libc++ bucket walk inline in operator[] | `0x675B8` | 179 | `0x3671EC` |

The three retained generic helpers are named
`ttstr_equal_backingAware_guess`.  Android arm64 inlines the same decision tree
inside `LabelValueMap_findBucketPredecessor_guess`; no artificial standalone
function was created there.

## 3. Backing layout used by equality

The `ttstr` value itself is one backing pointer.  Equality accesses:

| ABI | `ttstr` size | backing `Length` | backing `Hint` (hash layer only) |
|---|---:|---:|---:|
| LP64 | `0x08` | `+0x3C` | `+0x44` |
| ILP32 | `0x04` | `+0x34` | `+0x3C` |

Length is a 32-bit field on all four targets.  Equality never reads Hint; the
table includes it only to show that the V221 cache word is a distinct later
field.

Payload pointers are obtained only after both backings are known nonnull and
their Length fields match.  The content helper compares 16-bit code units and
returns negative/zero/positive; equality consumes only the zero result.

## 4. Bucket admission precedes equality

The native unordered lookup order is:

1. compute/reuse the lookup key's 32-bit Hint hash;
2. derive the target bucket;
3. walk nodes belonging to that bucket;
4. compare each node's cached hash with the lookup hash;
5. only when the hash matches, run backing-aware `ttstr` equality;
6. on equality true, return the existing mapped `double`;
7. otherwise continue or insert a new node.

Therefore a cached-hash mismatch prevents even backing-pointer identity from
being tested.  This is why externally mutating a shared Hint after insertion
can make an otherwise equal lookup miss: equality is correct, but the caller
has violated the stable-hash invariant before reaching it.

Neither node equality nor UTF-16 content comparison repairs, recomputes, or
publishes Hint.

## 5. Backing-pointer fast path

Pointer equality returns true immediately and bypasses Length/payload reads.
This covers:

- two copied `ttstr` aliases sharing one backing;
- comparing a stored node key with another alias of that exact backing;
- two default-constructed strings, because both backing pointers are null.

This is a pure identity fast path.  It performs no AddRef/Release and does not
change either string's ownership or cache state.

## 6. Exactly-one-null boundary

If pointer identity failed, all four helpers require both backings to be
nonnull.  Exactly one null returns false before dereferencing either Length.

Consequently:

| lhs | rhs | equality | hash |
|---|---|---:|---:|
| null/default | null/default | true | `0` / `0` |
| null/default | allocated empty | false | `0` / `UINT32_MAX` |
| allocated empty | null/default | false | `UINT32_MAX` / `0` |

The default-null and allocated-empty states may therefore coexist as two
distinct keys in one `unordered_map<ttstr,V>`.  This is intentional and does
not violate the equal-keys-must-have-equal-hash rule because the keys are not
equal.

This differs from a hypothetical comparator that blindly calls `c_str()` on
both values: KiriKiri's null projection points at an empty literal, which would
erase the backing-state distinction.  The portable functor must continue to
delegate to backing-aware `ttstr::operator==`.

## 7. Length and UTF-16 payload gates

For two different nonnull backings:

1. compare their 32-bit Length fields;
2. return false immediately on mismatch;
3. obtain payload pointers;
4. compare zero-terminated UTF-16 code units;
5. return true only when the content result is zero.

The Length gate avoids payload work for obvious mismatches, but it is not the
final identity: same-length different content remains unequal.

Two independently allocated empty strings have:

- different backing pointers;
- equal Length `0`;
- empty UTF-16 payloads;
- equal `UINT32_MAX` hashes after V221 computation.

They are therefore equal and address the same unordered node.  Assigning
through the second key updates the first key's mapped value rather than
inserting a third node beside null/default and allocated-empty.

## 8. Portable-source and regression alignment

`cpp/plugins/motionplayer/internal/ttstr_hash.h` now documents the full
backing-aware `ttstr_equal` contract immediately above its delegation to
`operator==`.  The executable expression is unchanged.

`tests/unit-tests/plugins/motionplayer-ttstr-hash-test.cpp` now covers:

- separate backings with equal text;
- different-length and same-length/different-content rejection;
- two null backings comparing equal;
- copied allocated-empty aliases comparing equal by backing identity;
- two independently allocated empty backings comparing equal by Length/payload;
- both null/allocated-empty operand orders comparing unequal;
- distinct Hint addresses for independent empty allocations and a shared Hint
  address for an alias;
- `LabelValueMap` holding null and allocated-empty as two distinct nodes;
- a second independently allocated empty key updating the existing
  allocated-empty node without increasing map size.

These tests also join the hash and equality halves: the two distinct empty
states are verified through real `LabelValueMap::operator[]`/`find` calls, not
only by calling the functors independently.

## 9. Recovery-IDB writeback

All four databases were processed sequentially and saved/closed:

- 5 semantic `_guess` function renames: two Android bucket-predecessor helpers
  plus the three retained generic equality helpers;
- 5 function type applications;
- 24 comments covering cached-hash admission, pointer identity, null gating,
  Length offsets, UTF-16 comparison, and empty-state consequences;
- 12 bookmarks.

Android arm64 records equality at its real inlined sites rather than inventing
a helper.  Final IDA session audit returned zero open sessions.

## 10. Validation and products

- the dedicated `motionplayer-ttstr-hash-test.cpp` TU passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax-only compilation;
- Web rebuilt all 35 affected steps and linked successfully;
- Wasmtime rebuilt all 65 affected native/guest steps and linked successfully;
- both CTest trees remain configured with `ENABLE_TESTS=false`, so they report
  no registered runnable tests;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- product sizes, hashes, and section sizes remain byte-identical to V222/V221,
  as expected from a comment-only production edit.

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

- the active Web/Wasmtime configurations syntax-compile but do not register the
  Catch2 executable, so the new runtime assertions are not executed by CTest in
  these trees;
- equality assumes valid KiriKiri string backings and does not defend against a
  dangling or corrupted backing pointer/Length;
- externally changing Hint after insertion remains an unsupported stable-hash
  violation even though equality itself ignores Hint;
- heterogeneous `const tjs_char *` comparison is not used by the audited
  `unordered_map<ttstr,V>` collision path and is not promoted into this native
  container contract;
- this closes the equality half of one shared container-key boundary and does
  not complete the full motionplayer recovery goal.
