# Geometry Hit-Test Differential Cases

## Scope

This skill currently covers only the standalone wasm differential suite for:

- `tests/differential/specs/geometry_hit_test/*.json`
- `tests/differential/python/run_geometry_hit_test_wasmtime.py`
- `tests/differential/wasm/geometry_hit_test_wasm.cpp`
- `cpp/plugins/motionplayer/HitTestInternal.h`

Do not expand to other differential families unless the user explicitly asks.

## Case Schema

Each case is one JSON file:

```json
{
  "id": "circle_inside",
  "family": "hit_test",
  "shape": {
    "kind": "circle",
    "cx": 0.0,
    "cy": 0.0,
    "r": 5.0
  },
  "point": {
    "x": 3.0,
    "y": 4.0
  }
}
```

Supported `shape.kind` payloads:

- `circle`: `cx`, `cy`, `r`
- `rect`: `left`, `top`, `right`, `bottom`
- `quad`: `x0,y0,x1,y1,x2,y2,x3,y3`
- Optional `type_override` is only for invalid-type coverage.

## Expected Semantics

Expected results should reflect intended/oracle-aligned behavior:

- Circle: boundary is inclusive because the check is `<= r*r`.
- Rect: left/top are inclusive, right/bottom are exclusive.
- Quad: inside points should return `true`; outside points should return `false`; winding-order coverage should keep the intended semantic expectation even if the current implementation still fails.
- Invalid type: return `false`.

Do not rewrite expectations to match temporary local bugs.

## Files To Touch

Normal case-authoring work usually touches only:

1. One or more JSON files in `tests/differential/specs/geometry_hit_test/`
2. `EXPECTED_HITS` in `tests/differential/python/run_geometry_hit_test_wasmtime.py`

Only touch these when explicitly required:

- `tests/differential/wasm/geometry_hit_test_wasm.cpp`
- `cpp/plugins/motionplayer/HitTestInternal.h`

Changing those means you are modifying the test harness or hit-test implementation, not just writing cases.

## Validation Commands

```bash
cmake --preset "Web Debug Config"
cmake --build out/web/debug --target geometry_hit_test_wasm
python3 -m pip install -r tests/differential/python/requirements-wasm.txt
python3 tests/differential/python/run_geometry_hit_test_wasmtime.py \
  --spec-dir tests/differential/specs/geometry_hit_test \
  --wasm out/web/debug/tests/differential/wasm/geometry_hit_test.wasm
```

## Current Known State

At the time this skill was created:

- circle cases pass
- rect cases pass
- invalid-type case passes
- quad expectation cases currently fail in local wasm execution

Treat the quad failures as a signal about implementation alignment, not as a reason to weaken the cases.
