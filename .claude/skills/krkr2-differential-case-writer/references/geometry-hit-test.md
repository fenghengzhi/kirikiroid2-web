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
- Quad: inside points should return `true`; outside points should return `false`; winding-order coverage keeps the intended semantic expectation regardless of the current implementation result.
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

Replace `<python>` with the current machine's Python 3 executable before running these commands.

```bash
cmake --preset "Web Debug Config"
cmake --build out/web/debug --target geometry_hit_test_wasm
<python> -m pip install -r tests/differential/python/requirements-wasm.txt
<python> tests/differential/python/run_geometry_hit_test_wasmtime.py \
  --spec-dir tests/differential/specs/geometry_hit_test \
  --wasm out/web/debug/tests/differential/wasm/geometry_hit_test.wasm
```

## Result Reporting

Pass/fail status is runtime state, not durable skill documentation. Before reporting current status, build `geometry_hit_test_wasm` and run the driver above against the present checkout. If an expected case fails, treat it as an implementation-alignment signal rather than weakening the expected result.
