# MotionPlayer dead render summary/local-rect helpers: four-binary record (2026-08-16)

## Conclusion

Two externally linked functions in the local render-detail namespace had a
declaration and definition but no production, test, registration, or cross-
plugin caller:

```text
summarizeLayerChildren(layer, maxChildren)
localRectFromItem(preparedItem)
```

Both were still emitted as strong symbols in the Web motionplayer archive.
They were removed after a fresh four-reference render audit.

`summarizeLayerChildren` was a port-only diagnostic formatter: it traversed
Layer children, built owning `std::string`/`fmt` output, exposed raw pointers and
names, and returned literals absent from every current reference binary.  The
native canvas submitters have no corresponding formatting or child-summary
call chain.

`localRectFromItem` only returned a zero-origin rectangle from clip width and
height.  It had no caller.  The native canvas/alpha-mask paths compute their
clip, overlap, viewport, and four outside strips directly in the owning
function; the live local `computeRenderClipRect` and
`clearLayerAlphaOutsideRect` already represent those recovered boundaries.

## Four-reference function map

| Target | complete canvas submitter | alpha-mask compositor |
|---|---:|---:|
| Android arm64 | `0x6C4820` | `0x6AC4E4` |
| Android armv7 | `0x58E2CC` | `0x57E1E8` |
| iOS arm64 | `0x1001186E0` | `0x100104E68` |
| iOS armv7 | `0x11653C` | `0x10243C` |

The canvas functions are the complete native boundaries; local
`renderToCanvas_guess` and execute helpers are source-level splits of that one
function, not extra reference functions.  Addresses are therefore kept in this
analysis record rather than compiled-source comments.

## Diagnostic-string exclusion

The deleted summary contained distinctive strings:

```text
<null-layer>
visibleChildren
selfVisible
```

For every term and every reference database, the audit performed:

- IDA string-table search;
- raw ASCII/UTF-8 byte search;
- UTF-16LE byte search;
- UTF-32LE byte search.

All 36 term/encoding/target combinations returned zero matches.  This agrees
with the complete canvas call-set audit: the native submitters call Layer/TJS,
source resolution, geometry and mask operations, but not `fmt`, string-stream,
path, logger, trace, child-summary, or diagnostic projection routines.

## Geometry boundary

The four canvas submitters directly:

1. intersect the prepared item's paint box with the canvas;
2. optionally floor/ceil a valid viewport and intersect it;
3. reject an empty ordered intersection;
4. use the resulting coordinates in direct/buffered submission.

The four alpha-mask compositors independently clip the requested destination
rectangle against the target Layer clip.  For supported mode plus `op == 1`,
they construct and submit the left, right, top, and bottom outside strips
directly before processing the overlap.  Neither native chain consumes a
standalone zero-origin rectangle derived only from prepared-item clip width and
height.

This does not claim that an optimized binary can prove the absence of every
possible inlined source expression.  The actionable source fact is stronger:
the local out-of-line `localRectFromItem` symbol had no caller at all and was
not part of either recovered owner/call boundary.

## Source effect

Removed from `PlayerRenderInternal.h/.cpp`:

- both declarations;
- both definitions;
- the summary formatter's unused Layer-child traversal and owning diagnostic
  string path.

Kept unchanged:

- `prepareLayerForRender`;
- `computeRenderClipRect`;
- `clearLayerAlphaOutsideRect`;
- the complete native-aligned canvas and alpha-mask execution paths;
- opt-in Web diagnostic code that still has an actual caller.

## Validation

- Ordinary and `KRKR2_WASMTIME_HEADLESS` Emscripten syntax checks passed for
  the complete motionplayer test translation unit, with only the existing
  `_tss` warning.
- Web Debug and Wasmtime Headless Debug `motionplayer` targets each rebuilt and
  linked 10 affected steps successfully.
- The complete Web Debug target performed a fresh final HTML/Wasm link
  successfully.
- Exact source searches found zero deleted identifiers.
- `llvm-nm -C` found neither former strong symbol in the rebuilt Web archive.
- The live clip and alpha-outside helpers retain their declarations,
  definitions, production calls, and focused clip tests.
- Targeted `git diff --check` and trailing-whitespace checks passed.
- Four canvas submitters and four alpha-mask compositors were force-recompiled;
  canvas entry-line comments and alpha function comments were read back, and
  all four recovery IDBs were saved in place.
