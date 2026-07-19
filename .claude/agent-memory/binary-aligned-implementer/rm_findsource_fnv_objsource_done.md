---
name: rm-findsource-fnv-objsource-done
description: ResourceManager::findSource @0x6AAB3C restored with mapped PSBFile raw-node navigation and ObjSource raw owner/node/texture lifetime
metadata:
  type: project
---

# ResourceManager::findSource / ObjSource — corrected verdict (2026-07-19)

`motion::ResourceManager::findSource` is aligned to `sub_6AAB3C @0x6AAB3C`.
The older conclusion that HashMap A stored a TJS module dictionary and that
ObjSource wrapped a `tTJSVariant` dict was disproven by the mapped-record
constructor/destructor and ObjSource construction/consumer chain.

## Authoritative mapped record

HashMap A is populated by RM `load`. Its mapped record owns, in declaration
order, a raw `PSBFile`, a Win `ttstr -> texture` map and a KRKR flat
source-descriptor map. Local `LoadedResourceRecord` mirrors this topology,
ownership and erase/clear lifetime. The outer map uses ttstr hash/equality and
is rehashed to 10 buckets.

## Binary structure

1. Split name by `/`; empty first component returns void.
2. Non-`src`: only `blank` is accepted and materialises width/height/origin data.
3. `src`: look up HashMap A by the original module/context key; miss returns void.
4. Read `record.file.GetRoot()`, then fixed-key strict / dynamic-key has+strict
   navigate `source/group/icon/name`; any dynamic miss returns void.
5. Allocate 0x18-byte ObjSource `{raw owner,node,null texture}`, AddRef owner, and
   call `CreateAdaptor(sticky=false,err=false)`; adaptor-null returns void without
   deleting the new object.

Local maps these steps through `LoadedResourceRecord::file`,
`PSBRawNode::{GetDictionaryValueStrict,ContainsDictionaryKey}` and
`ObjSource(PSBRawNode)`. No TJS PropGet dictionary walk remains on this path.

## ObjSource lifecycle and consumers

ObjSource is live on this route. originX/originY are strict raw reads;
width/height default to 32 only for a non-dictionary raw node; clip is try-gated
then strictly reads four edges. ensureTexture restores raw/RL8/RL32, palette,
aligned buffer, pitch-copy and texture creation flow. drawLayer consumes the
texture's own dimensions. Destruction releases texture first and raw owner
second.

Attribution remains important: `0x6AAB3C` is registered only by ResourceManager
at `0x6AB8BC`; it is different from `Player::findSource @0x6948E8`.

Current verdict: raw source topology, data flow, adaptor failure boundary and
ObjSource lifetime are closed. The older qword[1]-as-dictionary-tail, PropGet
facade and dead/unconstructed ObjSource claims must not be reused. KRKR full-page
upload remains a separately documented Web rendering API boundary.
