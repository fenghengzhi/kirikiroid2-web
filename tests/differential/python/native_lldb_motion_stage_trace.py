#!/usr/bin/env python3
"""LLDB-backed native 6-stage trace collector for motion_playback.

The script launches the macOS native runner under LLDB and records a staged
diagnostic stream that mirrors the Android Frida stage oracle shape. It keeps
the native executable as a full-engine launcher; all observations are made via
debug symbols and LLDB value inspection.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import sys
import time
from collections.abc import Iterable
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
SCHEMA = "motion-stage-oracle-v1"
EVENT_SCHEMA = "motion-stage-oracle-v1-event"
SOURCE = "native-lldb-macos"

STAGES: tuple[str, ...] = (
    "static_parse",
    "init_motion",
    "variable_binding",
    "frame_selection",
    "sub_motion_decision",
    "trace_flatten",
)

PROGRESS_COMPAT_SYMBOL = "motion::Player::progressCompatMethod"
PHASE3_LAST_SYMBOL = "motion::Player::updateLayersPhase3_AnchorNode"
INIT_NON_EMOTE_SYMBOL = "motion::Player::initNonEmoteMotionLike_0x6B365C"
PARSE_PARAMETER_SYMBOL = "motion::Player::appendParameterEntryLike_0x6B1718"
PARSE_PARAMETER_LIST_SYMBOL = "motion::Player::parseParameterListLike_0x6B202C"
BIND_PARAMETER_SYMBOL = "motion::Player::bindParameterValueLike_0x6C4668"
EVALUATE_TIMELINE_SYMBOL = "evaluateTimelineLike_0x699AE4"
SUB_MOTION_SYMBOL = "motion::Player::updateLayersPhase3_MotionSubNode"

STATIC_PARSE_PROJECTION = "static_parse-semantic-v1"
STATIC_PARSE_SAMPLE_POINTS = {
    "init_non_emote_enter": "initNonEmoteMotionLike_0x6B365C.enter",
    "init_non_emote_leave": "initNonEmoteMotionLike_0x6B365C.leave",
    "parse_parameter_enter": "appendParameterEntryLike_0x6B1718.enter",
    "parse_parameter_leave": "appendParameterEntryLike_0x6B1718.leave",
    "parse_parameter_list_enter": "parseParameterListLike_0x6B202C.enter",
    "parse_parameter_list_leave": "parseParameterListLike_0x6B202C.leave",
}

INIT_MOTION_PROJECTION = "init-motion-semantic-v1"
INIT_MOTION_SAMPLE_POINTS = {
    "init_non_emote_enter": "initNonEmoteMotionLike_0x6B365C.enter",
    "init_non_emote_leave": "initNonEmoteMotionLike_0x6B365C.leave",
}

TRACE_FLATTEN_PROJECTION = "trace_flatten-semantic-v1"
TRACE_FLATTEN_SAMPLE_POINT = "progressCompat.phase3-end.pre-cleanup"
FRAME_SELECTION_PROJECTION_SPEC = json.loads(
    (
        REPO_ROOT / "tests" / "differential" /
        "motion_stage_projections" / "frame_selection_v1.json"
    ).read_text(encoding="utf-8")
)
FRAME_SELECTION_PROJECTION = str(FRAME_SELECTION_PROJECTION_SPEC["projection"])
FRAME_SELECTION_SAMPLE_POINT = str(FRAME_SELECTION_PROJECTION_SPEC["samplePoint"])
FRAME_SELECTION_NODE_FIELDS: tuple[str, ...] = tuple(
    str(field) for field in FRAME_SELECTION_PROJECTION_SPEC["nodeFields"]
)

CANONICAL_ADDR = {
    "init_motion": 0x6B365C,
    "parse_parameter": 0x6B1718,
    "parse_parameter_list": 0x6B202C,
    "bind_parameter": 0x6C4668,
    "evaluate_timeline": 0x699AE4,
    "sub_motion": 0x6BE0C0,
    "phase3_last": 0x6C0528,
}

ACTIVE_TRACER: "NativeMotionStageTracer | None" = None
LLDB_INVALID_ADDRESS = (1 << 64) - 1


def _native_lldb_motion_stage_callback(frame, bp_loc, _internal_dict):
    if ACTIVE_TRACER is None:
        return False
    breakpoint_id = bp_loc.GetBreakpoint().GetID()
    ACTIVE_TRACER.handle_breakpoint_callback(breakpoint_id, frame)
    return False


def _load_lldb():
    try:
        lldb_python = subprocess.check_output(
            ["xcrun", "lldb", "-P"],
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
    except Exception as exc:  # pragma: no cover - host tool dependent
        raise RuntimeError(
            "failed to locate LLDB Python support. Check these commands:\n"
            "  xcrun lldb -P\n"
            "  xcrun python3 -c 'import sys; "
            "sys.path.insert(0, __import__(\"subprocess\").check_output("
            "[\"xcrun\", \"lldb\", \"-P\"], text=True).strip()); "
            "import lldb; print(lldb.SBDebugger)'"
        ) from exc

    if lldb_python and lldb_python not in sys.path:
        sys.path.insert(0, lldb_python)

    try:
        import lldb  # type: ignore
    except Exception as exc:  # pragma: no cover - host tool dependent
        raise RuntimeError(
            "failed to import LLDB Python module. Run this tracer through "
            "`xcrun python3`, or verify Xcode Command Line Tools with:\n"
            "  xcrun lldb -P\n"
            "  xcrun python3 -c 'import sys; "
            "sys.path.insert(0, __import__(\"subprocess\").check_output("
            "[\"xcrun\", \"lldb\", \"-P\"], text=True).strip()); "
            "import lldb; print(lldb.SBDebugger)'"
        ) from exc
    return lldb


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Trace motion_playback native stages via LLDB")
    p.add_argument("--runner", required=True,
                   help="Path to motion_playback_native")
    p.add_argument("--startup-xp3", required=True,
                   help="Path to logo_test_oracle.xp3")
    p.add_argument("--trace-out", required=True,
                   help="Path to write the raw stage event stream")
    p.add_argument("--expected-frames", type=int, default=332,
                   help="Expected trace_flatten frame count")
    p.add_argument("--timeout", type=float, default=90.0,
                   help="Soft timeout checked between LLDB stops")
    p.add_argument("--repo-root", default=str(REPO_ROOT),
                   help="Repository root for process working directory")
    p.add_argument("--stages", default="all",
                   help="Comma-separated stage list, or all")
    return p.parse_args(argv)


def selected_stages(raw: str) -> set[str]:
    if raw == "all":
        return set(STAGES)
    out = {item.strip() for item in raw.split(",") if item.strip()}
    unknown = sorted(out - set(STAGES))
    if unknown:
        raise ValueError(f"unknown stage(s): {unknown}; expected {STAGES}")
    out.add("trace_flatten")
    return out


def ptr_to_hex(value: int | None) -> str | None:
    if not value:
        return None
    return f"0x{value:x}"


def sb_unsigned(value, default: int = 0) -> int:
    if not value or not value.IsValid():
        return default
    try:
        return int(value.GetValueAsUnsigned(default))
    except Exception:
        raw = value.GetValue()
        return int(raw, 0) if raw else default


def sb_unsigned_optional(value) -> int | None:
    if not value or not value.IsValid():
        return None
    try:
        return int(value.GetValueAsUnsigned(0))
    except Exception:
        raw = value.GetValue()
        return int(raw, 0) if raw else None


def sb_signed(value, default: int = 0) -> int:
    if not value or not value.IsValid():
        return default
    try:
        return int(value.GetValueAsSigned(default))
    except Exception:
        raw = value.GetValue()
        return int(raw, 0) if raw else default


def sb_child(value, name: str, fallback_index: int | None = None):
    child = value.GetChildMemberWithName(name)
    if child.IsValid():
        return child
    if fallback_index is not None and value.GetNumChildren() > fallback_index:
        child = value.GetChildAtIndex(fallback_index)
        if child.IsValid():
            return child
    raise RuntimeError(
        f"LLDB value `{value.GetName()}` has no child `{name}`")


def sb_child_optional(value, *names: str):
    if not value or not value.IsValid():
        return None
    for name in names:
        child = value.GetChildMemberWithName(name)
        if child.IsValid():
            return child
    return None


def sb_bool(value, default: bool | None = None) -> bool | None:
    if not value or not value.IsValid():
        return default
    raw = value.GetValue()
    if raw is None:
        return default
    if raw in ("true", "false"):
        return raw == "true"
    try:
        return int(raw, 0) != 0
    except Exception:
        return default


def sb_float(value, default: float | None = None) -> float | None:
    if not value or not value.IsValid():
        return default
    raw = value.GetValue()
    if raw is None:
        return default
    try:
        f = float(raw)
        return f if math.isfinite(f) else default
    except Exception:
        return default


def sb_string(value) -> str | None:
    if not value or not value.IsValid():
        return None
    summary = value.GetSummary()
    if summary is None:
        raw = value.GetValue()
        return raw if raw is not None else None
    if len(summary) >= 2 and summary[0] == '"' and summary[-1] == '"':
        return bytes(summary[1:-1], "utf-8").decode("unicode_escape")
    return summary


def register_double(frame, name: str = "d0") -> float | None:
    reg = frame.FindRegister(name)
    if not reg.IsValid():
        return None
    raw = reg.GetValue()
    if raw is None:
        return None
    try:
        value = float(raw)
        return value if math.isfinite(value) else None
    except Exception:
        return None


def register_raw(frame, name: str) -> str | None:
    reg = frame.FindRegister(name)
    return reg.GetValue() if reg.IsValid() else None


def bool_return_register(frame) -> int | None:
    w0 = sb_unsigned_optional(frame.FindRegister("w0"))
    if w0 in (0, 1):
        return w0
    x0 = sb_unsigned_optional(frame.FindRegister("x0"))
    if x0 in (0, 1):
        return x0
    return x0 if x0 is not None else w0


def create_value_from_load_address(target, name: str, addr: int, value_type):
    return target.CreateValueFromAddress(
        name, target.ResolveLoadAddress(addr), value_type)


class NativeMotionStageTracer:
    def __init__(
        self,
        *,
        lldb,
        runner: Path,
        startup_xp3: Path,
        repo_root: Path,
        expected_frames: int,
        timeout: float,
        stages: Iterable[str],
    ) -> None:
        self.lldb = lldb
        self.runner = runner
        self.startup_xp3 = startup_xp3
        self.repo_root = repo_root
        self.expected_frames = expected_frames
        self.timeout = timeout
        self.enabled_stages = set(stages)
        self.enabled_stages.add("trace_flatten")
        self.full_trace_flatten = (
            self.enabled_stages == {"trace_flatten"} or
            self.enabled_stages == set(STAGES)
        )
        self.events: list[dict[str, Any]] = []
        self.seq_counter = 0
        self.frame_counter = 0
        self.current_record: dict[str, Any] | None = None
        self.record_stack: list[dict[str, Any]] = []
        self.progress_return_records: dict[int, dict[str, Any]] = {}
        self.return_records: dict[int, dict[str, Any]] = {}
        self.callback_errors: list[str] = []
        self.node_layout: dict[str, int] | None = None
        self.start_monotonic = 0.0

        self.compat_bp_id: int | None = None
        self.phase3_bp_id: int | None = None
        self.init_bp_id: int | None = None
        self.parse_bp_id: int | None = None
        self.parse_list_bp_id: int | None = None
        self.bind_bp_id: int | None = None
        self.eval_timeline_bp_id: int | None = None
        self.sub_motion_bp_id: int | None = None

    def run(self) -> list[dict[str, Any]]:
        global ACTIVE_TRACER
        lldb = self.lldb
        debugger = lldb.SBDebugger.Create()
        debugger.SetAsync(False)
        try:
            ACTIVE_TRACER = self
            self.start_monotonic = time.monotonic()
            target = debugger.CreateTarget(str(self.runner))
            if not target or not target.IsValid():
                raise RuntimeError(f"failed to create LLDB target: {self.runner}")

            self.compat_bp_id = self._required_bp(
                target, PROGRESS_COMPAT_SYMBOL).GetID()
            self.phase3_bp_id = self._required_bp(
                target, PHASE3_LAST_SYMBOL).GetID()
            self.init_bp_id = self._required_bp(
                target, INIT_NON_EMOTE_SYMBOL).GetID()

            if "variable_binding" in self.enabled_stages:
                bind_bp = self._optional_bp(target, BIND_PARAMETER_SYMBOL)
                self.bind_bp_id = bind_bp.GetID() if bind_bp else None

            if "static_parse" in self.enabled_stages:
                self.parse_bp_id = self._required_bp(
                    target, PARSE_PARAMETER_SYMBOL).GetID()
                self.parse_list_bp_id = self._required_bp(
                    target, PARSE_PARAMETER_LIST_SYMBOL).GetID()

            if "frame_selection" in self.enabled_stages:
                eval_timeline_bp = self._optional_symbol_start_bp(
                    target, EVALUATE_TIMELINE_SYMBOL)
                if eval_timeline_bp is None:
                    raise RuntimeError(
                        "failed to set frame_selection breakpoint on "
                        f"{EVALUATE_TIMELINE_SYMBOL}; rebuild the macOS Debug "
                        "native runner after the frame_selection refactor and "
                        "verify it with:\n"
                        "  nm -C <runner> | rg evaluateTimelineLike_0x699AE4"
                    )
                self.eval_timeline_bp_id = eval_timeline_bp.GetID()

            if "sub_motion_decision" in self.enabled_stages:
                self.sub_motion_bp_id = self._required_bp(
                    target, SUB_MOTION_SYMBOL).GetID()

            launch = lldb.SBLaunchInfo([
                "--startup-xp3",
                str(self.startup_xp3),
            ])
            launch.SetWorkingDirectory(str(self.repo_root))
            error = lldb.SBError()
            process = target.Launch(launch, error)
            if not error.Success():
                raise RuntimeError(f"LLDB launch failed: {error.GetCString()}")

            deadline = time.monotonic() + self.timeout
            while True:
                if self.callback_errors:
                    process.Kill()
                    raise RuntimeError("; ".join(self.callback_errors))
                if self.expected_frames and self.frame_counter >= self.expected_frames:
                    process.Kill()
                    break
                state = process.GetState()
                if state == lldb.eStateExited:
                    break
                if state not in (lldb.eStateStopped, lldb.eStateRunning):
                    raise RuntimeError(f"unexpected LLDB process state: {state}")
                if time.monotonic() > deadline:
                    process.Kill()
                    raise RuntimeError(
                        f"native stage LLDB trace timed out after "
                        f"{self.timeout:.1f}s with {self.frame_counter} "
                        "trace_flatten frame(s)"
                    )
                cont_error = process.Continue()
                if not cont_error.Success():
                    raise RuntimeError(
                        f"LLDB continue failed: {cont_error.GetCString()}")

            if self.expected_frames and self.frame_counter < self.expected_frames:
                raise RuntimeError(
                    f"native stage LLDB trace captured only "
                    f"{self.frame_counter} trace_flatten frame(s); expected "
                    f"at least {self.expected_frames}"
                )
            return self.events
        finally:
            ACTIVE_TRACER = None
            lldb.SBDebugger.Destroy(debugger)

    def _required_bp(self, target, name: str):
        bp = target.BreakpointCreateByName(name)
        if bp.GetNumLocations() < 1:
            raise RuntimeError(
                f"failed to set breakpoint on {name}; build the native runner "
                "with macOS Debug symbols"
            )
        self._install_auto_callback(bp)
        return bp

    def _optional_bp(self, target, name: str):
        bp = target.BreakpointCreateByName(name)
        if bp.GetNumLocations() < 1:
            try:
                target.BreakpointDelete(bp.GetID())
            except Exception:
                pass
            return None
        self._install_auto_callback(bp)
        return bp

    def _optional_symbol_start_bp(self, target, name: str):
        try:
            addr = self._symbol_start_address(target, name)
        except RuntimeError:
            return None
        bp = target.BreakpointCreateBySBAddress(addr)
        if bp.GetNumLocations() < 1:
            try:
                target.BreakpointDelete(bp.GetID())
            except Exception:
                pass
            return None
        self._install_auto_callback(bp)
        return bp

    def _symbol_start_address(self, target, name: str):
        contexts = target.FindFunctions(name, self.lldb.eFunctionNameTypeAuto)
        for index in range(contexts.GetSize()):
            context = contexts.GetContextAtIndex(index)
            for item in (context.GetFunction(), context.GetSymbol()):
                if not item or not item.IsValid():
                    continue
                start = item.GetStartAddress()
                if not start or not start.IsValid():
                    continue
                file_addr = start.GetFileAddress()
                if file_addr == LLDB_INVALID_ADDRESS:
                    continue
                addr = target.ResolveFileAddress(file_addr)
                if addr and addr.IsValid():
                    return addr
        raise RuntimeError(f"failed to resolve symbol start address: {name}")

    def _install_auto_callback(self, breakpoint) -> None:
        error = breakpoint.SetScriptCallbackBody(
            "import __main__\n"
            "return __main__._native_lldb_motion_stage_callback("
            "frame, bp_loc, internal_dict)"
        )
        if not error.Success():
            raise RuntimeError(
                f"failed to install LLDB breakpoint callback: "
                f"{error.GetCString()}")
        breakpoint.SetAutoContinue(True)

    def _set_return_breakpoint(self, frame, payload: dict[str, Any]) -> int:
        ret_addr = self._callee_return_address(frame)
        if not ret_addr:
            raise RuntimeError("breakpoint had no callee return address")
        target = frame.GetThread().GetProcess().GetTarget()
        ret_bp = target.BreakpointCreateByAddress(ret_addr)
        ret_bp.SetOneShot(True)
        try:
            ret_bp.SetThreadID(frame.GetThread().GetThreadID())
        except Exception:
            pass
        self._install_auto_callback(ret_bp)
        self.return_records[ret_bp.GetID()] = payload
        return ret_bp.GetID()

    @staticmethod
    def _callee_return_address(frame) -> int | None:
        try:
            thread = frame.GetThread()
            if thread and thread.IsValid() and thread.GetNumFrames() > 1:
                caller = thread.GetFrameAtIndex(1)
                if caller and caller.IsValid():
                    pc = int(caller.GetPC())
                    if pc and pc != LLDB_INVALID_ADDRESS:
                        return pc
        except Exception:
            pass
        for reg_name in ("x30", "lr"):
            value = sb_unsigned_optional(frame.FindRegister(reg_name))
            if value:
                return value
        return None

    def handle_breakpoint_callback(self, breakpoint_id: int, frame) -> None:
        try:
            if breakpoint_id == self.compat_bp_id:
                self._on_progress_enter(frame)
            elif breakpoint_id == self.phase3_bp_id:
                self._on_phase3_last_enter(frame)
            elif breakpoint_id == self.init_bp_id:
                self._on_init_motion_enter(frame)
            elif breakpoint_id == self.parse_bp_id:
                self._on_parse_parameter_enter(frame)
            elif breakpoint_id == self.parse_list_bp_id:
                self._on_parse_parameter_list_enter(frame)
            elif breakpoint_id == self.bind_bp_id:
                self._on_bind_parameter_enter(frame)
            elif breakpoint_id == self.eval_timeline_bp_id:
                self._on_evaluate_timeline_enter(frame)
            elif breakpoint_id == self.sub_motion_bp_id:
                self._on_sub_motion_enter(frame)
            elif breakpoint_id in self.progress_return_records:
                self._on_progress_return(breakpoint_id, frame)
            elif breakpoint_id in self.return_records:
                self._on_generic_return(breakpoint_id, frame)
        except Exception as exc:
            self.callback_errors.append(str(exc))

    def _emit(self, stage: str, kind: str, payload: dict[str, Any] | None = None) -> None:
        if stage not in self.enabled_stages:
            return
        ev = dict(payload or {})
        ev["schema"] = EVENT_SCHEMA
        ev["stage"] = stage
        ev["kind"] = kind
        ev["seq"] = self.seq_counter
        ev["timeMs"] = int((time.monotonic() - self.start_monotonic) * 1000)
        if self.current_record is not None:
            ev.setdefault("frameId", self.current_record.get("frameId"))
            if stage != "trace_flatten":
                if stage == "frame_selection":
                    diagnostics = dict(ev.get("diagnostics") or {})
                    diagnostics.setdefault(
                        "objthis", self.current_record.get("objthis"))
                    ev["diagnostics"] = diagnostics
                else:
                    ev.setdefault("objthis", self.current_record.get("objthis"))
        self.seq_counter += 1
        self.events.append(ev)

    def _emit_static_parse(
        self,
        kind: str,
        payload: dict[str, Any] | None = None,
        diagnostics: dict[str, Any] | None = None,
    ) -> None:
        if "static_parse" not in self.enabled_stages:
            return
        diag = dict(diagnostics or {})
        if self.current_record is not None:
            diag.setdefault("frameId", self.current_record.get("frameId"))
            diag.setdefault("objthis", self.current_record.get("objthis"))
        ev = dict(payload or {})
        ev["schema"] = EVENT_SCHEMA
        ev["stage"] = "static_parse"
        ev["kind"] = kind
        ev["projection"] = STATIC_PARSE_PROJECTION
        ev["samplePoint"] = STATIC_PARSE_SAMPLE_POINTS.get(kind, kind)
        ev["diagnostics"] = diag
        ev["seq"] = self.seq_counter
        ev["timeMs"] = int((time.monotonic() - self.start_monotonic) * 1000)
        self.seq_counter += 1
        self.events.append(ev)

    def _emit_init_motion(
        self,
        kind: str,
        payload: dict[str, Any] | None = None,
        diagnostics: dict[str, Any] | None = None,
    ) -> None:
        if "init_motion" not in self.enabled_stages:
            return
        diag = dict(diagnostics or {})
        if self.current_record is not None:
            diag.setdefault("frameId", self.current_record.get("frameId"))
            diag.setdefault("objthis", self.current_record.get("objthis"))
        ev = dict(payload or {})
        ev["schema"] = EVENT_SCHEMA
        ev["stage"] = "init_motion"
        ev["kind"] = kind
        ev["projection"] = INIT_MOTION_PROJECTION
        ev["samplePoint"] = INIT_MOTION_SAMPLE_POINTS.get(kind, kind)
        ev["diagnostics"] = diag
        ev["seq"] = self.seq_counter
        ev["timeMs"] = int((time.monotonic() - self.start_monotonic) * 1000)
        self.seq_counter += 1
        self.events.append(ev)

    # ----------------------------------------------------------- progress/flat

    def _on_progress_enter(self, frame) -> None:
        objthis = ptr_to_hex(sb_unsigned(frame.FindRegister("x3")))
        record: dict[str, Any] = {
            "frameId": self.frame_counter,
            "objthis": objthis,
            "players": [],
            "errors": [],
        }
        ret_id = self._set_return_breakpoint(frame, {
            "kind": "progress_return",
            "record": record,
        })
        self.progress_return_records[ret_id] = self.return_records.pop(ret_id)
        self.record_stack.append(record)
        self.current_record = record

    def _on_phase3_last_enter(self, frame) -> None:
        if self.current_record is None:
            return
        player = self._player_ptr_from_frame(frame)
        self._set_return_breakpoint(frame, {
            "kind": "phase3_last_return",
            "record": self.current_record,
            "player": player,
        })

    def _on_progress_return(self, breakpoint_id: int, frame) -> None:
        info = self.progress_return_records.pop(breakpoint_id, None)
        self._delete_bp(frame, breakpoint_id)
        if info is None:
            return
        record = info["record"]
        flat_layers: list[dict[str, Any]] = []
        diagnostic_players: list[dict[str, Any]] = []
        players = record.get("players") or []
        for player in players:
            layer_start = len(flat_layers)
            for layer in player["layers"]:
                out = dict(layer)
                out["index"] = len(flat_layers)
                flat_layers.append(out)
            diagnostic_players.append({
                "ptr": player.get("ptr"),
                "layout": player.get("layout"),
                "layerStart": layer_start,
                "layerCount": len(player.get("layers") or []),
                "error": player.get("error"),
            })
        errors = record.get("errors") or []
        self._emit("trace_flatten", "frame", {
            "projection": TRACE_FLATTEN_PROJECTION,
            "samplePoint": TRACE_FLATTEN_SAMPLE_POINT,
            "frameId": record.get("frameId"),
            "playerCount": len(players),
            "layers": flat_layers,
            "diagnostics": {
                "objthis": record.get("objthis"),
                "topPlayer": players[0]["ptr"] if players else None,
                "layout": "pre-cleanup",
                "players": diagnostic_players,
                "error": "; ".join(errors) if errors else None,
            },
        })
        self.frame_counter += 1
        if self.record_stack and self.record_stack[-1] is record:
            self.record_stack.pop()
        else:
            self.record_stack = [r for r in self.record_stack if r is not record]
        self.current_record = self.record_stack[-1] if self.record_stack else None

    # ------------------------------------------------------------- stage enter

    def _on_init_motion_enter(self, frame) -> None:
        player = self._player_ptr_from_frame(frame)
        self._emit_static_parse("init_non_emote_enter", {}, {
            "addr": CANONICAL_ADDR["init_motion"],
            "player": player,
        })
        self._emit_init_motion("init_non_emote_enter", {}, {
            "addr": CANONICAL_ADDR["init_motion"],
            "player": player,
        })
        self._set_return_breakpoint(frame, {
            "kind": "init_motion_return",
            "player": player,
        })

    def _on_parse_parameter_enter(self, frame) -> None:
        x0 = ptr_to_hex(sb_unsigned(frame.FindRegister("x0")))
        x1 = ptr_to_hex(sb_unsigned(frame.FindRegister("x1")))
        self._emit_static_parse("parse_parameter_enter", {}, {
            "addr": CANONICAL_ADDR["parse_parameter"],
            "x0": x0,
            "x1": x1,
        })
        self._set_return_breakpoint(frame, {
            "kind": "parse_parameter_return",
            "x0": x0,
            "x1": x1,
        })

    def _on_parse_parameter_list_enter(self, frame) -> None:
        x0 = ptr_to_hex(sb_unsigned(frame.FindRegister("x0")))
        x1 = ptr_to_hex(sb_unsigned(frame.FindRegister("x1")))
        self._emit_static_parse("parse_parameter_list_enter", {}, {
            "addr": CANONICAL_ADDR["parse_parameter_list"],
            "x0": x0,
            "x1": x1,
        })
        self._set_return_breakpoint(frame, {
            "kind": "parse_parameter_list_return",
            "x0": x0,
            "x1": x1,
        })

    def _on_bind_parameter_enter(self, frame) -> None:
        player = self._player_ptr_from_frame(frame)
        mode = sb_signed(frame.FindRegister("x2"), 0)
        value = register_double(frame, "d0")
        label_ptr = ptr_to_hex(sb_unsigned(frame.FindRegister("x1")))
        label = sb_string(frame.FindVariable("label"))
        before = self._parameter_table_for_player_ptr(frame, player)
        self._emit("variable_binding", "bind_parameter_enter", {
            "addr": CANONICAL_ADDR["bind_parameter"],
            "player": player,
            "labelPtr": label_ptr,
            "label": label,
            "mode": mode,
            "value": value,
            "valueRaw": register_raw(frame, "d0"),
            "parameterTableBefore": before,
        })
        self._set_return_breakpoint(frame, {
            "kind": "bind_parameter_return",
            "player": player,
            "labelPtr": label_ptr,
            "label": label,
            "mode": mode,
            "value": value,
            "valueRaw": register_raw(frame, "d0"),
            "before": before,
        })

    def _on_evaluate_timeline_enter(self, frame) -> None:
        node_ptr = ptr_to_hex(sb_unsigned(frame.FindRegister("x0")))
        before = self._semantic_frame_selection_node(
            self._node_brief_from_ptr(frame, node_ptr))
        self._set_return_breakpoint(frame, {
            "kind": "evaluate_timeline_return",
            "node": node_ptr,
            "dirtyArg": sb_signed(frame.FindRegister("x1"), 0),
            "time": register_double(frame, "d0"),
            "timeRaw": register_raw(frame, "d0"),
            "before": before,
        })

    def _on_sub_motion_enter(self, frame) -> None:
        player = self._player_ptr_from_frame(frame)
        samples_before = self._current_sample_players()
        before = self._snapshot_motion_sub_nodes(frame, player)
        self._set_return_breakpoint(frame, {
            "kind": "sub_motion_return",
            "player": player,
            "samplesBefore": samples_before,
            "before": before,
        })

    # ------------------------------------------------------------ return cases

    def _on_generic_return(self, breakpoint_id: int, frame) -> None:
        info = self.return_records.pop(breakpoint_id, None)
        self._delete_bp(frame, breakpoint_id)
        if info is None:
            return
        kind = info.get("kind")
        if kind == "phase3_last_return":
            self._on_phase3_last_return(frame, info)
        elif kind == "init_motion_return":
            self._on_init_motion_return(frame, info)
        elif kind == "parse_parameter_return":
            self._emit_static_parse("parse_parameter_leave", {}, {
                "addr": CANONICAL_ADDR["parse_parameter"],
                "x0": info.get("x0"),
                "x1": info.get("x1"),
                "retval": ptr_to_hex(sb_unsigned(frame.FindRegister("x0"))),
            })
        elif kind == "parse_parameter_list_return":
            self._emit_static_parse("parse_parameter_list_leave", {}, {
                "addr": CANONICAL_ADDR["parse_parameter_list"],
                "x0": info.get("x0"),
                "x1": info.get("x1"),
                "retval": ptr_to_hex(sb_unsigned(frame.FindRegister("x0"))),
            })
        elif kind == "bind_parameter_return":
            self._on_bind_parameter_return(frame, info)
        elif kind == "evaluate_timeline_return":
            self._on_evaluate_timeline_return(frame, info)
        elif kind == "sub_motion_return":
            self._on_sub_motion_return(frame, info)

    def _on_phase3_last_return(self, frame, info: dict[str, Any]) -> None:
        record = info["record"]
        player = info["player"]
        try:
            layers = self._dump_layers(frame, player) \
                if self.full_trace_flatten else []
            record["players"].append({
                "ptr": player,
                "layout": "deque" if self.full_trace_flatten else "deque-segment",
                "layers": layers,
                "error": None,
            })
        except Exception as exc:
            record["errors"].append(str(exc))

    def _on_init_motion_return(self, frame, info: dict[str, Any]) -> None:
        player = info["player"]
        overview = self._player_overview(frame, player)
        retval = ptr_to_hex(sb_unsigned(frame.FindRegister("x0")))
        raw_parameter_table = overview.get("parameterTable")
        self._emit_static_parse("init_non_emote_leave", {
            "parameterTable": self._semantic_parameter_table(raw_parameter_table),
        }, {
            "addr": CANONICAL_ADDR["init_motion"],
            "retval": retval,
            "player": player,
            "parameterTable": self._parameter_table_diagnostics(raw_parameter_table),
        })
        self._emit_init_motion("init_non_emote_leave", {
            "overview": self._semantic_player_overview(overview),
        }, {
            "addr": CANONICAL_ADDR["init_motion"],
            "retval": retval,
            "player": player,
            "overview": self._player_overview_diagnostics(overview),
        })

    def _on_bind_parameter_return(self, frame, info: dict[str, Any]) -> None:
        after = self._parameter_table_for_player_ptr(frame, info["player"])
        self._emit("variable_binding", "bind_parameter_leave", {
            "addr": CANONICAL_ADDR["bind_parameter"],
            "player": info["player"],
            "labelPtr": info.get("labelPtr"),
            "label": info.get("label"),
            "mode": info.get("mode"),
            "value": info.get("value"),
            "valueRaw": info.get("valueRaw"),
            "retval": ptr_to_hex(sb_unsigned(frame.FindRegister("x0"))),
            "changedEntries": self._parameter_table_changes(
                info.get("before"), after),
            "parameterTableAfter": after,
        })

    def _on_evaluate_timeline_return(self, frame, info: dict[str, Any]) -> None:
        self._emit("frame_selection", "evaluate_timeline", {
            "projection": FRAME_SELECTION_PROJECTION,
            "samplePoint": FRAME_SELECTION_SAMPLE_POINT,
            "dirtyArg": info.get("dirtyArg"),
            "time": info.get("time"),
            "retval": bool_return_register(frame),
            "before": info.get("before"),
            "after": self._semantic_frame_selection_node(
                self._node_brief_from_ptr(frame, info.get("node"))),
            "diagnostics": {
                "addr": CANONICAL_ADDR["evaluate_timeline"],
                "node": info.get("node"),
                "timeRaw": info.get("timeRaw"),
            },
        })

    def _on_sub_motion_return(self, frame, info: dict[str, Any]) -> None:
        player = info["player"]
        samples_after = self._current_sample_players()
        child_delta = max(0, len(samples_after) - len(info.get("samplesBefore", [])))
        after = self._snapshot_motion_sub_nodes(frame, player)
        self._emit("sub_motion_decision", "sub_motion_decision", {
            "addr": CANONICAL_ADDR["sub_motion"],
            "player": player,
            "retval": ptr_to_hex(sb_unsigned(frame.FindRegister("x0"))),
            "childSamplesBefore": info.get("samplesBefore", []),
            "childSamplesAfter": samples_after,
            "childSampleDelta": child_delta,
            "decisions": self._compare_motion_sub_snapshots(
                info.get("before", []), after, child_delta),
        })

    # --------------------------------------------------------------- snapshots

    def _player_ptr_from_frame(self, frame) -> str:
        this_value = frame.FindVariable("this")
        ptr = sb_unsigned(this_value)
        if not ptr:
            ptr = sb_unsigned(frame.FindRegister("x0"))
        out = ptr_to_hex(ptr)
        if out is None:
            raise RuntimeError("breakpoint frame had no motion::Player this pointer")
        return out

    def _runtime_value_for_player_ptr(self, frame, player_ptr_hex: str):
        player = self._player_value_from_ptr(frame, player_ptr_hex)
        runtime_shared = sb_child(player, "_runtime", 0)
        runtime_ptr = sb_child(runtime_shared, "pointer", 0)
        if not sb_unsigned(runtime_ptr):
            raise RuntimeError("Player::_runtime pointer is null")
        return runtime_ptr.Dereference()

    def _player_overview(self, frame, player_ptr_hex: str) -> dict[str, Any]:
        player = self._player_value_from_ptr(frame, player_ptr_hex)
        runtime = self._runtime_value_for_player_ptr(frame, player_ptr_hex)
        count = self._node_count_for_player_ptr(frame, player_ptr_hex)
        overview = {
            "player": player_ptr_hex,
            "nodeLayout": "deque",
            "nodeCount": count,
            "parameterTable": self._parameter_table_from_runtime(runtime),
            "playing": sb_bool(sb_child_optional(player, "_allplaying"), None),
            "currentTime": sb_float(sb_child_optional(player, "_clampedEvalTime")),
            "frameTickCount": sb_float(sb_child_optional(player, "_frameTickCount")),
            "frameLastTime": sb_float(sb_child_optional(player, "_frameLastTime")),
        }
        return overview

    @staticmethod
    def _semantic_player_overview(raw: dict[str, Any] | None) -> dict[str, Any]:
        raw = raw or {}
        return {
            "nodeCount": raw.get("nodeCount", 0),
            "parameterTable": NativeMotionStageTracer._semantic_parameter_table(
                raw.get("parameterTable")),
            "playing": raw.get("playing"),
            "currentTime": raw.get("currentTime"),
        }

    @staticmethod
    def _player_overview_diagnostics(raw: dict[str, Any] | None) -> dict[str, Any]:
        raw = raw or {}
        return {
            "player": raw.get("player"),
            "nodeLayout": raw.get("nodeLayout"),
            "frameTickCount": raw.get("frameTickCount"),
            "frameLastTime": raw.get("frameLastTime"),
            "parameterTable": NativeMotionStageTracer._parameter_table_diagnostics(
                raw.get("parameterTable")),
        }

    def _parameter_table_for_player_ptr(self, frame, player_ptr_hex: str) -> dict[str, Any]:
        runtime = self._runtime_value_for_player_ptr(frame, player_ptr_hex)
        return self._parameter_table_from_runtime(runtime)

    def _parameter_table_from_runtime(self, runtime) -> dict[str, Any]:
        entries = sb_child(runtime, "parameterEntries").GetNonSyntheticValue()
        try:
            data_ptr, end_ptr, elem_type, stride, count = self._vector_span(entries)
        except Exception as exc:
            return {
                "begin": None,
                "end": None,
                "stride": None,
                "count": 0,
                "entries": [],
                "error": str(exc),
            }
        out = {
            "begin": ptr_to_hex(data_ptr),
            "end": ptr_to_hex(end_ptr),
            "stride": stride,
            "count": count,
            "defaultParameterEntryIndex":
                sb_signed(sb_child_optional(runtime, "defaultParameterEntryIndex"), -1),
            "defaultParameterEntry":
                ptr_to_hex(sb_unsigned(sb_child_optional(runtime, "defaultParameterEntryPtr"))),
            "entries": [],
        }
        if count > 256:
            out["error"] = "parameter vector unexpectedly large"
            return out
        target = runtime.GetTarget()
        for i in range(count):
            entry = create_value_from_load_address(
                target, "parameterEntry", data_ptr + i * stride, elem_type)
            out["entries"].append(self._read_parameter_entry(entry, i))
        return out

    @staticmethod
    def _semantic_parameter_table(raw: dict[str, Any] | None) -> dict[str, Any]:
        entries = (raw or {}).get("entries") or []
        return {
            "count": (raw or {}).get("count", len(entries)),
            "entries": [
                {
                    "index": entry.get("index"),
                    "id": entry.get("id"),
                    "discretization": bool(entry.get("discretization")),
                    "rangeBegin": entry.get("rangeBegin"),
                    "rangeEnd": entry.get("rangeEnd"),
                    "value": entry.get("value"),
                    "mode": entry.get("mode"),
                }
                for entry in entries
            ],
        }

    @staticmethod
    def _parameter_table_diagnostics(raw: dict[str, Any] | None) -> dict[str, Any]:
        raw = raw or {}
        diag = {
            "begin": raw.get("begin"),
            "end": raw.get("end"),
            "stride": raw.get("stride"),
            "defaultParameterEntryIndex": raw.get("defaultParameterEntryIndex"),
            "defaultParameterEntry": raw.get("defaultParameterEntry"),
        }
        if raw.get("error"):
            diag["error"] = raw.get("error")
        entries = raw.get("entries") or []
        if entries:
            diag["entries"] = [
                {
                    "index": entry.get("index"),
                    "ptr": entry.get("ptr"),
                    "idPtr": entry.get("idPtr"),
                }
                for entry in entries
            ]
        return diag

    def _read_parameter_entry(self, entry, index: int) -> dict[str, Any]:
        id_value = sb_child_optional(entry, "id")
        return {
            "index": index,
            "ptr": ptr_to_hex(sb_unsigned(entry.AddressOf())),
            "idPtr": ptr_to_hex(sb_unsigned(id_value.AddressOf()))
                     if id_value is not None else None,
            "id": sb_string(id_value),
            "discretization": sb_bool(sb_child_optional(entry, "discretization")),
            "rangeBegin": sb_float(sb_child_optional(entry, "rangeBegin")),
            "rangeEnd": sb_float(sb_child_optional(entry, "rangeEnd")),
            "value": sb_float(sb_child_optional(entry, "value")),
            "mode": sb_signed(sb_child_optional(entry, "mode"), 0),
        }

    def _snapshot_eval_nodes(self, frame, player_ptr_hex: str) -> list[dict[str, Any]]:
        return self._snapshot_nodes(frame, player_ptr_hex, motion_only=False)

    def _snapshot_motion_sub_nodes(self, frame, player_ptr_hex: str) -> list[dict[str, Any]]:
        return self._snapshot_nodes(frame, player_ptr_hex, motion_only=True)

    def _snapshot_nodes(
        self,
        frame,
        player_ptr_hex: str,
        *,
        motion_only: bool,
    ) -> list[dict[str, Any]]:
        runtime = self._runtime_value_for_player_ptr(frame, player_ptr_hex)
        count = self._node_count_for_player_ptr(frame, player_ptr_hex)
        if count > 10000:
            raise RuntimeError(f"runtime nodes deque unexpectedly large: {count}")
        out: list[dict[str, Any]] = []
        for i in range(count):
            node = self._node_value_for_player_index(frame, player_ptr_hex, i)
            brief = self._read_node_brief(node, i, runtime)
            if motion_only and brief.get("nodeType") != 3:
                continue
            out.append(brief)
        return out

    def _read_node_brief(self, node, index: int, runtime=None) -> dict[str, Any]:
        accumulated = sb_child_optional(node, "accumulated")
        param_index = sb_signed(sb_child_optional(node, "parameterizeIndex"), -1)
        param = self._parameter_for_node(runtime, param_index) if runtime else None
        return {
            "index": index,
            "ptr": ptr_to_hex(sb_unsigned(node.AddressOf())),
            "parameterEntry": param.get("ptr") if param else None,
            "parameter": self._node_parameter_payload(param),
            "coordinateMode": sb_signed(sb_child_optional(node, "coordinateMode"), 0),
            "nodeType": sb_signed(sb_child_optional(node, "nodeType"), 0),
            "parentIndex": sb_signed(sb_child_optional(node, "parentIndex"), -1),
            "flags": sb_unsigned(sb_child_optional(node, "flags"), 0),
            "activeSlot": sb_signed(sb_child_optional(node, "activeSlotIndex"), 0),
            "active": sb_bool(sb_child_optional(accumulated, "active"), None),
            "visible": sb_bool(sb_child_optional(accumulated, "visible"), None),
            "opacity": sb_signed(sb_child_optional(accumulated, "opacity"), 0),
        }

    @staticmethod
    def _semantic_frame_selection_node(
        raw: dict[str, Any] | None,
    ) -> dict[str, Any] | None:
        if raw is None:
            return None
        return {
            field: raw.get(field)
            for field in FRAME_SELECTION_NODE_FIELDS
        }

    @staticmethod
    def _node_parameter_payload(param: dict[str, Any] | None) -> dict[str, Any] | None:
        if not param:
            return None
        return {
            "ptr": param.get("ptr"),
            "mode": param.get("mode"),
            "value": param.get("value"),
            "id": param.get("id"),
        }

    def _node_brief_from_ptr(
        self,
        frame,
        node_ptr_hex: str | None,
    ) -> dict[str, Any] | None:
        if not node_ptr_hex:
            return None
        target = frame.GetThread().GetProcess().GetTarget()
        node_type = target.FindFirstType("motion::detail::MotionNode")
        if not node_type.IsValid():
            node_type = target.FindFirstType("motion::MotionNode")
        if not node_type.IsValid():
            node_type = target.FindFirstType("MotionNode")
        if not node_type.IsValid():
            return {
                "ptr": node_ptr_hex,
                "error": "motion::detail::MotionNode debug type not found",
            }
        node = create_value_from_load_address(
            target, "node", int(node_ptr_hex, 16), node_type)
        return self._read_node_brief(node, -1, None)

    def _parameter_for_node(self, runtime, param_index: int) -> dict[str, Any] | None:
        table = self._parameter_table_from_runtime(runtime)
        entries = table.get("entries") or []
        if 0 <= param_index < len(entries):
            return dict(entries[param_index])
        default_index = table.get("defaultParameterEntryIndex", -1)
        if isinstance(default_index, int) and 0 <= default_index < len(entries):
            return dict(entries[default_index])
        return None

    def _current_sample_players(self) -> list[str]:
        if self.current_record is None:
            return []
        return [
            str(player["ptr"]) for player in self.current_record.get("players", [])
            if player.get("ptr")
        ]

    @staticmethod
    def _parameter_table_changes(before, after) -> list[dict[str, Any]]:
        changes: list[dict[str, Any]] = []
        b = (before or {}).get("entries") or []
        a = (after or {}).get("entries") or []
        for i in range(max(len(b), len(a))):
            bi = b[i] if i < len(b) else None
            ai = a[i] if i < len(a) else None
            if bi is None or ai is None:
                changes.append({
                    "index": i,
                    "before": bi,
                    "after": ai,
                    "reason": "entry_added_or_removed",
                })
                continue
            if bi.get("mode") != ai.get("mode") or bi.get("value") != ai.get("value"):
                changes.append({
                    "index": i,
                    "id": ai.get("id") if ai.get("id") is not None else bi.get("id"),
                    "beforeMode": bi.get("mode"),
                    "afterMode": ai.get("mode"),
                    "beforeValue": bi.get("value"),
                    "afterValue": ai.get("value"),
                })
        return changes

    @staticmethod
    def _classify_sub_motion(before_node, after_node, child_sample_delta: int) -> str:
        node = after_node or before_node or {}
        param = node.get("parameter") or {}
        mode = param.get("mode")
        if mode is None:
            mode = 0
        flags = node.get("flags") or 0
        if child_sample_delta > 0:
            return "play_or_update_child"
        if (int(mode) & 5) != 0 or int(flags) != 0:
            return "gate_open_no_child_sample"
        if node.get("visible") is False and int(mode) == 0:
            return "skip_invisible"
        return "skip_gate_closed"

    def _compare_motion_sub_snapshots(
        self,
        before: list[dict[str, Any]],
        after: list[dict[str, Any]],
        child_sample_delta: int,
    ) -> list[dict[str, Any]]:
        by_index: dict[int, dict[str, Any]] = {}
        for b in before or []:
            by_index[int(b["index"])] = {"before": b, "after": None}
        for a in after or []:
            entry = by_index.setdefault(int(a["index"]),
                                        {"before": None, "after": None})
            entry["after"] = a
        out: list[dict[str, Any]] = []
        for index in sorted(by_index):
            item = by_index[index]
            out.append({
                "index": index,
                "before": item.get("before"),
                "after": item.get("after"),
                "decision": self._classify_sub_motion(
                    item.get("before"), item.get("after"), child_sample_delta),
            })
        return out

    # --------------------------------------------------------------- raw layers

    def _dump_layers(self, frame, player_ptr_hex: str | None = None) -> list[dict[str, Any]]:
        if player_ptr_hex:
            player = self._player_value_from_ptr(frame, player_ptr_hex)
        else:
            this_value = frame.FindVariable("this")
            if this_value.IsValid() and sb_unsigned(this_value):
                player = this_value.Dereference()
                player_ptr_hex = ptr_to_hex(sb_unsigned(player.AddressOf()))
            else:
                raise RuntimeError("phase3 return frame has no `this` variable")
        if not player_ptr_hex:
            raise RuntimeError("could not resolve motion::Player pointer")
        count = self._node_count_for_player_ptr(frame, player_ptr_hex)
        if count == 0:
            return []
        if count > 10000:
            raise RuntimeError(f"runtime nodes deque is unexpectedly large: {count}")

        layers: list[dict[str, Any]] = []
        sequence, synthetic_count = self._node_sequence_for_player_ptr(
            frame, player_ptr_hex)
        if sequence is not None:
            count = synthetic_count
        for i in range(count):
            if sequence is not None:
                node = sequence.GetChildAtIndex(i)
            else:
                node = self._node_value_for_player_index(
                    frame, player_ptr_hex, i)
            layers.append(self._read_layer_from_node(node, i))
        return layers

    def _node_count_for_player_ptr(self, frame, player_ptr_hex: str) -> int:
        _sequence, count = self._node_sequence_for_player_ptr(
            frame, player_ptr_hex)
        if count is not None:
            return count
        expr = (
            f"reinterpret_cast<motion::Player *>({player_ptr_hex})"
            "->runtime()->nodes.size()"
        )
        value = frame.EvaluateExpression(expr)
        if not value.IsValid() or not value.GetError().Success():
            err = value.GetError().GetCString() if value.IsValid() else "invalid value"
            raise RuntimeError(f"failed to evaluate node deque size: {err}")
        return sb_unsigned(value)

    def _node_value_for_player_index(self, frame, player_ptr_hex: str, index: int):
        sequence, count = self._node_sequence_for_player_ptr(frame, player_ptr_hex)
        if sequence is not None and count is not None and index < count:
            node = sequence.GetChildAtIndex(index)
            if node.IsValid():
                return node
        expr = (
            "(unsigned long long)(&("
            f"reinterpret_cast<motion::Player *>({player_ptr_hex})"
            f"->runtime()->nodes[{index}]))"
        )
        value = frame.EvaluateExpression(expr)
        if not value.IsValid() or not value.GetError().Success():
            err = value.GetError().GetCString() if value.IsValid() else "invalid value"
            raise RuntimeError(f"failed to evaluate node deque element {index}: {err}")
        addr = sb_unsigned(value)
        if not addr:
            raise RuntimeError(f"node deque element {index} has null address")
        target = frame.GetThread().GetProcess().GetTarget()
        node_type = self._node_type(target)
        return create_value_from_load_address(target, "node", addr, node_type)

    def _node_sequence_for_player_ptr(self, frame, player_ptr_hex: str):
        try:
            runtime = self._runtime_value_for_player_ptr(frame, player_ptr_hex)
            nodes = sb_child_optional(runtime, "nodes")
            if not nodes or not nodes.IsValid():
                return None, None
            synthetic = nodes.GetSyntheticValue()
            if not synthetic.IsValid():
                return None, None
            count = synthetic.GetNumChildren()
            if count < 0:
                return None, None
            if count == 0:
                return synthetic, 0
            first = synthetic.GetChildAtIndex(0)
            type_name = first.GetTypeName() if first.IsValid() else ""
            if first.IsValid() and "MotionNode" in type_name:
                return synthetic, count
        except Exception:
            return None, None
        return None, None

    @staticmethod
    def _node_type(target):
        for name in (
            "motion::detail::MotionNode",
            "motion::MotionNode",
            "MotionNode",
        ):
            node_type = target.FindFirstType(name)
            if node_type.IsValid():
                return node_type
        raise RuntimeError("motion::detail::MotionNode debug type not found")

    @staticmethod
    def _read_layer_from_node(node, index: int) -> dict[str, Any]:
        accumulated = sb_child_optional(node, "accumulated")
        return {
            "index": index,
            "nodeType": sb_signed(sb_child_optional(node, "nodeType"), 0),
            "visible": sb_bool(sb_child_optional(accumulated, "visible"), None),
            "active": sb_bool(sb_child_optional(accumulated, "active"), None),
            "flipX": sb_bool(sb_child_optional(accumulated, "flipX"), None),
            "flipY": sb_bool(sb_child_optional(accumulated, "flipY"), None),
            "posX": sb_float(sb_child_optional(accumulated, "posX")),
            "posY": sb_float(sb_child_optional(accumulated, "posY")),
            "posZ": sb_float(sb_child_optional(accumulated, "posZ")),
            "angleDeg": sb_float(sb_child_optional(accumulated, "angle")),
            "scaleX": sb_float(sb_child_optional(accumulated, "scaleX")),
            "scaleY": sb_float(sb_child_optional(accumulated, "scaleY")),
            "slantX": sb_float(sb_child_optional(accumulated, "slantX")),
            "slantY": sb_float(sb_child_optional(accumulated, "slantY")),
            "opacity": sb_signed(sb_child_optional(accumulated, "opacity"), 0),
            "stencilType": sb_signed(sb_child_optional(node, "stencilType"), 0),
        }

    def _player_value_from_ptr(self, frame, player_ptr_hex: str):
        target = frame.GetThread().GetProcess().GetTarget()
        addr = int(player_ptr_hex, 16)
        player_type = target.FindFirstType("motion::Player")
        if player_type.IsValid():
            value = create_value_from_load_address(
                target, "player", addr, player_type)
            if value.IsValid():
                return value
        value = frame.EvaluateExpression(
            f"reinterpret_cast<motion::Player *>({player_ptr_hex})")
        if value.IsValid() and value.GetError().Success():
            return value.Dereference()
        err = value.GetError().GetCString() if value.IsValid() else "invalid value"
        raise RuntimeError(
            f"could not materialize motion::Player at {player_ptr_hex}: {err}")

    def _nodes_vector(self, runtime):
        return sb_child(runtime, "nodes").GetNonSyntheticValue()

    def _vector_count(self, vector_value) -> int:
        data_ptr, end_ptr, _elem_type, stride, _count = self._vector_span(vector_value)
        if end_ptr < data_ptr:
            raise RuntimeError("vector has invalid begin/end")
        return (end_ptr - data_ptr) // stride

    def _vector_span(self, vector_value):
        vec = vector_value.GetNonSyntheticValue()
        begin = sb_child(vec, "__begin_", 0)
        end = sb_child(vec, "__end_", 1)
        data_ptr = sb_unsigned(begin)
        end_ptr = sb_unsigned(end)
        elem_type = begin.GetType().GetPointeeType()
        stride = int(elem_type.GetByteSize())
        if stride <= 0:
            raise RuntimeError("vector element type has invalid size")
        if data_ptr == 0 and end_ptr == 0:
            return 0, 0, elem_type, stride, 0
        if end_ptr < data_ptr:
            raise RuntimeError("vector begin/end are inverted")
        span = end_ptr - data_ptr
        if span % stride != 0:
            raise RuntimeError(
                f"vector span {span} is not aligned to element size {stride}")
        return data_ptr, end_ptr, elem_type, stride, span // stride

    def _ensure_node_layout(self, node_type) -> dict[str, int]:
        if self.node_layout is not None:
            return self.node_layout

        fields = [
            "nodeType",
            "stencilType",
            "accumulated.visible",
            "accumulated.active",
            "accumulated.flipX",
            "accumulated.flipY",
            "accumulated.posX",
            "accumulated.posY",
            "accumulated.posZ",
            "accumulated.angle",
            "accumulated.scaleX",
            "accumulated.scaleY",
            "accumulated.slantX",
            "accumulated.slantY",
            "accumulated.opacity",
        ]
        layout = {"sizeof": int(node_type.GetByteSize())}
        for field in fields:
            key = field.rsplit(".", 1)[-1]
            if field == "stencilType":
                key = "stencilType"
            elif field == "nodeType":
                key = "nodeType"
            layout[key] = self._field_offset(node_type, field)
        self.node_layout = layout
        return layout

    @staticmethod
    def _field_offset(root_type, field_path: str) -> int:
        current = root_type
        offset = 0
        for name in field_path.split("."):
            member = None
            for i in range(current.GetNumberOfFields()):
                candidate = current.GetFieldAtIndex(i)
                if candidate.GetName() == name:
                    member = candidate
                    break
            if member is None:
                raise RuntimeError(
                    f"debug type `{current.GetName()}` has no field `{name}`")
            offset += int(member.GetOffsetInBytes())
            current = member.GetType()
        return offset

    def _delete_bp(self, frame, breakpoint_id: int) -> None:
        try:
            frame.GetThread().GetProcess().GetTarget().BreakpointDelete(breakpoint_id)
        except Exception:
            pass

    @staticmethod
    def _read_bool(blob: bytes, offset: int) -> bool:
        return blob[offset] != 0

    @staticmethod
    def _read_i32(blob: bytes, offset: int) -> int:
        return struct.unpack_from("<i", blob, offset)[0]

    @staticmethod
    def _read_f64(blob: bytes, offset: int) -> float | None:
        value = struct.unpack_from("<d", blob, offset)[0]
        return value if math.isfinite(value) else None


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    runner = Path(args.runner)
    startup_xp3 = Path(args.startup_xp3)
    trace_out = Path(args.trace_out)
    repo_root = Path(args.repo_root)

    if sys.platform != "darwin":
        print("native stage LLDB trace is only supported on macOS",
              file=sys.stderr)
        return 2
    if not runner.exists():
        print(f"native runner not found: {runner}", file=sys.stderr)
        return 2
    if not startup_xp3.exists():
        print(f"startup xp3 not found: {startup_xp3}", file=sys.stderr)
        return 2

    try:
        lldb = _load_lldb()
        tracer = NativeMotionStageTracer(
            lldb=lldb,
            runner=runner,
            startup_xp3=startup_xp3,
            repo_root=repo_root,
            expected_frames=args.expected_frames,
            timeout=args.timeout,
            stages=selected_stages(args.stages),
        )
        events = tracer.run()
        trace_out.parent.mkdir(parents=True, exist_ok=True)
        trace_out.write_text(
            json.dumps(events, ensure_ascii=True, allow_nan=False) + "\n",
            encoding="utf-8",
        )
    except Exception as exc:
        print(f"native stage LLDB trace failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
