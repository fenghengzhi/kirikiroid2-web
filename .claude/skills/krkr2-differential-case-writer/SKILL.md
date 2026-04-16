---
name: krkr2-differential-case-writer
description: Write and update KrKr2 differential test cases for the current geometry_hit_test port-wasm/wasmtime scheme. Use when adding or editing JSON cases under tests/differential/specs/geometry_hit_test, updating EXPECTED_HITS in tests/differential/python/run_geometry_hit_test_wasmtime.py, or extending the current hit-test case set while preserving the existing case format, wasm ABI, and intended oracle-aligned expectations.
---

# Krkr2 Differential Case Writer

## Workflow

Treat the current scheme as a narrow contract:

1. Stay within `geometry_hit_test` unless the user explicitly asks to add a new differential family.
2. Read `references/geometry-hit-test.md` before writing or editing cases.
3. Update the JSON spec and `EXPECTED_HITS` together.
4. Keep expected results oracle-aligned. Do not lower expectations to match current local bugs.

## Write Cases

- Add or edit JSON files under `tests/differential/specs/geometry_hit_test/`.
- Keep the schema exactly:
  - `id`
  - `family: "hit_test"`
  - `shape.kind`
  - `shape` payload
  - `point.x`
  - `point.y`
  - optional `shape.type_override` only for invalid-type coverage
- Keep file name and `id` aligned: `case_id.json`.
- Prefer snake_case ids that describe behavior, such as `rect_right_bottom_exclusive`.

## Update Expectations

- Mirror every new case in `tests/differential/python/run_geometry_hit_test_wasmtime.py` under `EXPECTED_HITS`.
- Encode intended behavior, not current wasm output.
- For the current suite, circle and rect cases are stable. Quad cases currently expose a known mismatch; keep their expected values aligned with intended hit-test semantics instead of changing them to `false`.

## Validate

- Build the standalone wasm target:
  - `cmake --preset "Web Debug Config"`
  - `cmake --build out/web/debug --target geometry_hit_test_wasm`
- Run the wasmtime driver:
  - `python3 -m pip install -r tests/differential/python/requirements-wasm.txt`
  - `python3 tests/differential/python/run_geometry_hit_test_wasmtime.py --spec-dir tests/differential/specs/geometry_hit_test --wasm out/web/debug/tests/differential/wasm/geometry_hit_test.wasm`
- If a new case requires changing the wasm ABI or `flatten_case`, call that out explicitly because it is outside normal case-authoring work.

## References

- Read `references/geometry-hit-test.md` for the schema, semantics, touched files, and validation commands.
