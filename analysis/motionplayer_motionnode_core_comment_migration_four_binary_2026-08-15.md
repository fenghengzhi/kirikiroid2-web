# MotionPlayer MotionNode core-field comment migration — four-reference audit (2026-08-15)

## Scope and result

This pass removes the remaining `node+...` physical-offset annotations from
`MotionNode.h`. Those comments described one Android arm64 layout as if it were
the portable declaration. Most named fields were semantically correct, but the
offset identity was not: pointer width, Variant/string representation, deque
layout, and the two STL families move the same source-level fields differently
in all four current reference binaries.

The migration was based on fresh decompilation of the current MotionNode
constructors/common initializers, recursive tree builders, node-field
initializers, and vertex computation functions. It changes comments only; it
does not reorder portable members or alter runtime behavior. The separately
discovered synthetic clip-origin fields and active-slot snapshot behavior are
handled in
`motionplayer_active_slot_origin_no_cache_four_binary_2026-08-15.md` because
that phase required real source changes.

## Fresh function map

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| MotionNode constructor | `0x6EED94` | `0x5ACC70` | `0x10014151C` | `0x1425BC` |
| common-field initializer | `0x696770` | `0x572A2C` | `0x1000F6580` | `0xF316C` |
| recursive node builder | `0x6B1E4C` | `0x5818B0` | `0x100109328` | `0x106BDC` |
| raw node-field initializer | `0x6B1058` | `0x580FA4` | `0x100108720` | `0x105E70` |
| vertex computation | `0x6B98D0` | `0x5866F8` | `0x10010F6AC` | `0x10CE30` |

Fresh entity queries and decompilation also reconfirm node strides of 2632,
2272, 2648, and 2228 bytes respectively. The source-level object is common;
the four physical layouts are not.

V232 corrected the Android arm64 naming inherited by the earlier table:
`0x6EED94` is the construction body that establishes the member owners, while
`0x696770` is the common-field initializer it reaches. It must not be treated
as the complete constructor.

## Prefix and active-slot divergence

The recursive builders append/construct a node, mark both clip slots done,
store the parent index, read the raw label, and call `requireLayerId` twice
before invoking the raw-field initializer. Even these early fields differ:

| semantic field | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| first/second layer ID | `+16/+20` | `+8/+12` | `+16/+20` | `+8/+12` |
| node type | `+28` | `+20` | `+28` | `+20` |
| parent index | `+36` | `+28` | `+36` | `+28` |
| active-slot index | `+1392` | `+1160` | `+1392` | `+1128` |

Representative fresh stores are A64 `0x6B2170/0x6B220C`, A32
`0x581A4A/0x581A82`, I64 `0x1001094F8/0x100109534`, and I32
`0x106DA8/0x106DE4` for the two IDs. The old inline comments happened to match
the 64-bit ID offsets but were wrong for both 32-bit targets.

The declarations now say that the IDs are two independent
`ResourceManager.requireLayerId` results and that `nodeType` is the raw layer
`type`, without pretending those meanings imply one ABI offset.

## Transform-state divergence

The constructors/common initializers reset the two transform blocks and seed
their booleans, unit scales, and opacity. Their bases differ:

| logical block | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| accumulated/evaluated state | `+1504` | `+1264` | `+1520` | `+1232` |
| TJS delta/override state | `+1584` | `+1344` | `+1600` | `+1312` |

Each block remains 0x50 bytes at source level, but the surrounding member
layout moves it. Fresh constructor/common-init writes include A64
`0x696818..0x696874`, A32 `0x572AB8..0x572B12`, I64
`0x1000F662C..0x1000F6678`, and I32 `0xF3238..0xF32CE`.

Inline per-member `node+1584`, `node+1592`, and similar comments were therefore
removed. The enclosing source comments retain the meaningful distinction:
one block is script/root override delta state; the other is the evaluated and
parent-composed state.

## Mesh container and matrix divergence

The current vertex audit provides the following physical map:

| semantic field | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| has live mesh data | `+1962` | `+1694` | `+1978` | `+1658` |
| derived inheritance separator | `+1963` | `+1695` | `+1979` | `+1659` |
| raw meshCombine | `+1964` | `+1696` | `+1980` | `+1660` |
| mesh ancestor pointer | `+1968` | `+1700` | `+1984` | `+1664` |
| mesh type/flags/division | `+2000/+2004/+2008` | `+1720/+1724/+1728` | `+2016/+2020/+2024` | `+1684/+1688/+1692` |
| grid X/Y | `+2012/+2016` | `+1732/+1736` | `+2028/+2032` | `+1696/+1700` |
| three MeshPoint vectors | `+2024/+2048/+2072` | `+1740/+1752/+1764` | `+2040/+2064/+2088` | `+1704/+1716/+1728` |
| inverse matrix/offset block | `+2096..+2132` | `+1776..+1812` | `+2112..+2148` | `+1740..+1776` |

The declarations now preserve property identity, container roles, and the
raw/derived distinction while referring exact layouts to `analysis/`.
Architecture-specific vector-header widths are especially unsuitable for a
shared C++ field comment.

## Geometry, bounds, and source descriptor

The four-corner float output and four-float bounds also move:

| field | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| vertices[8] | `+1856` | `+1616` | `+1872` | `+1580` |
| bounds[4] | `+1888` | `+1648` | `+1904` | `+1612` |
| source.valid | `+200` | `+184` | `+200` | `+184` |
| source origin X/Y | `+248/+256` | `+224/+232` | `+248/+256` | `+220/+228` |

The portable comments now describe the corner order, AABB role, independent
Variant owner, and non-owning texture pointer. This slice originally retained a
a narrow Web path sidecar; V231 replaced it with the native-shaped retained
`ttstr`, while continuing to keep per-target offsets out of portable comments.
The iOS armv7 source origin already disproves a common layout even among 32-bit
targets.

## Source migration

`cpp/plugins/motionplayer/MotionNode.h` now contains no `node+<number>` layout
annotations. Semantic constants such as content masks remain because they are
cross-target data contracts, not addresses. Exact per-target offsets remain in
the focused four-reference analysis documents and recovery IDBs, where they
can be audited without conflating ABI layout with portable declaration order.
