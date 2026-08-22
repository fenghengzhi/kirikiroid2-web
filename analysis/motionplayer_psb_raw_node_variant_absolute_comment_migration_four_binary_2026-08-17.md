# motionplayer PSB raw-node / Variant absolute-comment migration four-binary audit

## 1. Scope and conclusion

After the V221 stale-provenance scan, the only remaining compiled unit-test
comments that embedded current-reference `sub_...` addresses were seven PSB
boundaries in `tests/unit-tests/plugins/psbfile-dll.cpp`.  These operations are
direct dependencies of motionplayer's `.psb`/`.mtn` load and resource paths:

- transfer of the one-pointer raw owner holder;
- retained root-node construction;
- nonthrowing dictionary lookup;
- dictionary containment and string category gates;
- storage-media container replacement;
- raw-node-to-TJS-Variant materialization, including resource/Octet copying.

All 28 function bodies were freshly decompiled from the four current reference
images before editing the test source.  The previous behavioral claims remain
valid, but the fresh cross-image read makes one aliasing limit explicit:
`GetDictionaryValue(key, out)` is release-first and has no self guard.
`out == this` works only while a second owner reference keeps the backing alive;
it is not generally self-assignment-safe.

No production behavior needed to change.  The compiled test comments now state
stable four-reference semantics without addresses; absolute mappings remain in
this analysis document only.

## 2. Four-image mapping and xrefs

| semantic `_guess` identity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | direct xrefs A64/A32/i64/i32 |
|---|---:|---:|---:|---:|---:|
| `PSBFile_transferHolder_guess` | `0x598E44` | `0x4DD350` | `0x1000ED8E4` | `0xE9BE2` | `1/1/1/2` |
| `PSBRawNode_containsDictionaryKey_guess` | `0x5999B8` | `0x4DD918` | `0x1000EDEF0` | `0xEA120` | `15/15/15/15` |
| `PSBRawNode_getString_guess` | `0x598F38` | `0x4DD3A0` | `0x1000ED94C` | `0xE9C90` | `8/8/8/8` |
| `PSBFile_getRootRawNode_guess` | `0x598E1C` | `0x4DD33A` | `0x1000ED8C8` | `0xE9BD0` | `1/1/1/1` |
| `PSBRawNode_tryGetDictionaryValue_guess` | `0x599138` | `0x4DD544` | `0x1000EDB08` | `0xE9E1C` | `9/9/9/9` |
| `PSBMedia_ensureContainer_guess` | `0x59A1E4` | `0x4DDF18` | `0x1000EE754` | `0xEA7F8` | `3/3/3/3` |
| `PSBValueDispatch_createVariant_guess` | `0x596B1C` | `0x4DBD78` | `0x1000EB9D0` | `0xE8308` | `4/4/4/4` |

The selected names describe the recovered source role but remain `_guess`
because private C++ identifiers do not survive in the stripped images.

## 3. Holder and raw-node structure

The common source-level model is:

```cpp
struct PSBFile {
    PSBRawOwner *owner;                 // one owning intrusive reference
};

struct PSBRawNode {
    PSBFile file;                       // owns/retains backing
    const uint8_t *node;                // borrowed pointer into backing bytes
};
```

Thus `PSBFile` is 8 bytes on LP64 and 4 bytes on ILP32; `PSBRawNode` is 16 and
8 bytes respectively.  The node pointer has no independent allocation or
reference count.  Keeping it valid depends entirely on the embedded holder's
owner reference.

`PSBRawOwner` uses an intrusive 32-bit counter.  `AddRef` increments; `Release`
decrements and destroys the owner at zero.  The holder assignment recovered in
all four images deliberately uses release-old, copy-pointer, AddRef order and
has no self-assignment check.

## 4. Transfer boundary

The source operation is structurally:

```cpp
PSBFile PSBFile::Transfer_guess() {
    PSBFile result(*this);              // copy pointer + AddRef
    *this = PSBFile();                  // Release old, install null
    return result;
}
```

For a valid positive incoming owner count, the AddRef/Release pair has zero net
count change and the returned holder becomes the sole replacement for the
source reference.  The source slot is null at return.

The lowering differs:

- Android armv7 and both iOS images visibly increment and call their shared
  Release helper before clearing source;
- Android arm64 folds the pair and keeps only the invalid-incoming-zero deletion
  edge plus source clear.

The source signature remains unwind-capable rather than `noexcept`; the unit
`static_assert` preserves that type-level distinction.  This is transfer-like
behavior, but the exact original special-member spelling is stripped.

## 5. Retained root-node construction

All four root helpers:

1. read `owner->header->entries` without a null-owner guard;
2. copy/retain the one-pointer `PSBFile` holder into the return node;
3. store the entries pointer as the independent raw node field.

Consequences:

- calling raw `GetRoot` on an empty holder dereferences null;
- the returned node survives destruction, reload, or reassignment of the
  original `PSBFile` because it owns a separate reference;
- only the script-facing root-dispatch getter supplies an empty-holder guard;
- node bytes themselves are never copied by this operation.

## 6. Nonthrowing dictionary lookup and alias boundary

The common flow is:

```text
nameIndex = find name trie index(key)
if miss: return false
valueOffset = find dictionary value offset(node + 1, nameIndex)
if miss: return false

child = node + 1 + valueOffset
out.file = this.file                 // release old, copy source, AddRef
out.node = child
return true
```

Both miss paths return before touching `out`; a caller's prior owner/node pair
is preserved exactly.

On hit, the child pointer is captured before destination replacement.  However,
the holder assignment releases `out`'s old owner before reloading and retaining
`this->owner`, with no identity test.  Therefore:

- ordinary distinct-source/destination use is safe;
- `out == this` is safe in the existing regression only because the live
  `PSBFile file` holds another owner reference;
- if the aliased raw node owns the last reference, the release can destroy the
  backing before the subsequent reload/AddRef, producing the shipped dangling
  boundary;
- the portable source must not normalize this to retain-first assignment or
  add a self guard if one-to-one behavior is the objective.

The `key` pointer is likewise trusted; these functions have no null-key guard.

## 7. ContainsDictionaryKey wrapper

Every image constructs a default/null temporary raw node before testing the
input node category.  Android inlines the tag classifier, while iOS calls the
shared classifier.  The logical body is:

```cpp
PSBRawNode temporary;
if (category(self.tag) != 7)
    return false;
return tryGetDictionaryValue(key, temporary);
```

The temporary is destroyed on every exit.  Known non-dictionary categories do
not touch dictionary trie/offset data and return false.  Category 7 delegates
to the same nonthrowing lookup above, so a hit temporarily retains the owner
until wrapper exit.

There is no null self/node guard before reading the tag.

## 8. GetString category and table boundary

All four implementations classify the raw tag first and return null for every
known non-category-4 value.  Only category 4 reaches:

1. the packed string-index decode (`0x15` through `0x18` width forms);
2. the owner's strings-offset packed array;
3. the owner's string-data base plus decoded offset.

Thus calling `GetString` on a dictionary/resource/numeric node does not touch
the strings table.  A valid string result is borrowed storage inside the owner;
it has no independent allocation or lifetime.

## 9. PSBMedia container replacement transaction

All four `EnsureContainer` bodies preserve this ordering:

```text
slash = name.IndexOf('/')
if slash < 0: return false
container = name.SubString(0, slash)
if _file is Object and _container == container: return true

candidate = new PSBFile
if candidate.LoadStorage(container) fails:
    delete candidate
    return false

object = CreateAdaptor(candidate)
nextFile = Void
if object != null:
    nextFile = object closure
    release factory reference
_file = nextFile
_container = container
return true
```

Important commit/lifetime edges:

- no-slash and load failure preserve the old `_file`/`_container` pair;
- adaptor creation failure still commits a Void `_file`, commits the new name,
  and reports success; the newly loaded native holder is left unclaimed;
- `_file` is committed before `_container`, so a string-assignment exception can
  leave a partial new-file/old-name state;
- streams returned by `Open` borrow raw resource bytes and do not retain the
  owner; replacing `_file` invalidates their data block even if the stream
  object itself still exists;
- after successful container selection, a missing resource makes `Open` throw
  the `cannot open psbfile` error; a container-load failure instead returns
  null without that throw.

## 10. CreateVariant resource/Octet ownership

The full function maps all raw categories, but the migrated regression targets
category 5 (resource -> TJS Octet).  In every image this branch:

1. decodes the resource pointer and byte count;
2. allocates a fresh Octet copy when the resource is non-null;
3. constructs a temporary `tvtOctet` Variant around that owner;
4. copy-assigns it into the caller's output Variant;
5. destroys the temporary.

The output therefore owns copied bytes and remains valid after every source
`PSBFile`, raw node, and dispatch holder is destroyed.  This differs from
`PSBMedia::Open`, whose memory stream borrows the raw owner block.

The same `CreateVariant` function creates fresh owner-sharing dispatch objects
for array/dictionary categories and scalar Variants for the remaining known
categories.  Both iOS images retain the postcondition assertion that an Object
Variant contains a non-null dispatch; Android release images strip it.

## 11. Source/test migration

`tests/unit-tests/plugins/psbfile-dll.cpp` no longer embeds 28 absolute
`binary!sub_address` tokens.  Its comments now identify the seven source-level
behaviors directly.  The aliased-output test also records why its live `file`
holder is essential and why it must not be generalized to sole-owner
self-assignment safety.

No runtime line in `cpp/plugins/psbfile` or `cpp/plugins/motionplayer` changed.
The pre-existing regressions continue to cover:

- transfer not being `noexcept`;
- dictionary category gates and false paths;
- string category gate and valid string lookup;
- root/raw-node owner retention across reload/destruction;
- miss preservation and hit replacement with aliased output;
- media container replacement versus stale borrowed stream data;
- copied Octet survival after all raw owners leave scope.

## 12. Recovery-IDB writeback

All databases were processed sequentially and saved/closed:

- 28 semantic `_guess` function renames;
- 20 safe function type applications (the two hidden-return ABI functions per
  image were intentionally left with their recovered ABI prototypes);
- 28 behavior/lifetime comments;
- 28 bookmarks.

The dictionary-lookup comment and bookmark in each image were corrected after
the release-first self-alias boundary was re-read.  Final IDA session audit
returned zero open sessions.

## 13. Validation and products

- the dedicated `psbfile-dll.cpp` TU passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax-only compilation;
- both Web and Wasmtime build trees reported `ninja: no work to do`, as expected
  because only a disabled unit-test TU comment changed;
- both configured CTest trees report no registered tests (`ENABLE_TESTS=false`);
- Node constructs both Wasm files successfully;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- product size, SHA-256, and section sizes remain byte-identical to V221/V220.

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

## 14. Limits

- this slice freshly rechecks seven high-value PSB boundaries but does not
  re-audit every branch of the large scalar/list/dictionary `CreateVariant`
  dispatcher;
- the sole-owner self-alias path is documented from exact instruction order and
  is not deliberately executed because it can destroy the backing mid-call;
- malformed packed offsets/indices remain trusted raw-data boundaries outside
  this provenance-migration slice;
- the old comprehensive PSB audit remains useful historical context, but this
  report and the saved current recovery databases are the authoritative V222
  mapping;
- this closes the remaining compiled-test absolute-address comments found by
  the V221 scan and does not complete the full motionplayer recovery goal.
