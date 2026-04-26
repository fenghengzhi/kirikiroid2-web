#!/usr/bin/env python3
"""LLDB-backed native trace collector for motion_playback.

This script launches the native macOS runner under LLDB, samples the
return from motion::Player::updateLayersPhase3_AnchorNode(), and writes
events matching the Android Frida oracle schema. That helper is the final
phase3 call inside updateLayers(), so its return is the same phase3-end,
pre-cleanup boundary used by the Android Frida tracer.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
PHASE3_LAST_SYMBOL = "motion::Player::updateLayersPhase3_AnchorNode"
ACTIVE_TRACER: "NativeMotionTracer | None" = None


def _native_lldb_motion_trace_callback(frame, bp_loc, _internal_dict):
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
    except Exception as exc:  # pragma: no cover - depends on host tools
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
    except Exception as exc:  # pragma: no cover - depends on host tools
        raise RuntimeError(
            "failed to import LLDB Python module. Run this verifier through "
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
        description="Trace motion_playback native runner via LLDB")
    p.add_argument("--runner", required=True,
                   help="Path to motion_playback_native")
    p.add_argument("--startup-xp3", required=True,
                   help="Path to logo_test_oracle.xp3")
    p.add_argument("--trace-out", required=True,
                   help="Path to write JSON trace events")
    p.add_argument("--expected-frames", type=int, default=332,
                   help="Minimum expected event count")
    p.add_argument("--timeout", type=float, default=90.0,
                   help="Soft timeout checked between LLDB stops")
    p.add_argument("--repo-root", default=str(REPO_ROOT),
                   help="Repository root for source line lookup")
    return p.parse_args(argv)


def ptr_to_hex(value: int | None) -> str | None:
    if not value:
        return None
    return f"0x{value:x}"


def sb_unsigned(value, default: int = 0) -> int:
    try:
        return int(value.GetValueAsUnsigned(default))
    except Exception:
        raw = value.GetValue()
        return int(raw, 0) if raw else default


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


class NativeMotionTracer:
    def __init__(
        self,
        *,
        lldb,
        runner: Path,
        startup_xp3: Path,
        repo_root: Path,
        expected_frames: int,
        timeout: float,
    ) -> None:
        self.lldb = lldb
        self.runner = runner
        self.startup_xp3 = startup_xp3
        self.repo_root = repo_root
        self.expected_frames = expected_frames
        self.timeout = timeout
        self.events: list[dict[str, Any]] = []
        self.frame_counter = 0
        self.current_record: dict[str, Any] | None = None
        self.compat_bp_id: int | None = None
        self.phase3_bp_id: int | None = None
        self.record_stack: list[dict[str, Any]] = []
        self.progress_return_records: dict[int, dict[str, Any]] = {}
        self.phase3_return_records: dict[int, dict[str, Any]] = {}
        self.callback_errors: list[str] = []
        self.node_layout: dict[str, int] | None = None

    def run(self) -> list[dict[str, Any]]:
        global ACTIVE_TRACER
        lldb = self.lldb
        debugger = lldb.SBDebugger.Create()
        debugger.SetAsync(False)
        try:
            ACTIVE_TRACER = self
            target = debugger.CreateTarget(str(self.runner))
            if not target or not target.IsValid():
                raise RuntimeError(f"failed to create LLDB target: {self.runner}")

            compat_bp = target.BreakpointCreateByName(
                "motion::Player::progressCompatMethod")
            if compat_bp.GetNumLocations() < 1:
                raise RuntimeError(
                    "failed to set breakpoint on "
                    "motion::Player::progressCompatMethod; build the native "
                    "runner with debug symbols"
                )
            self._install_auto_callback(compat_bp)

            phase3_bp = target.BreakpointCreateByName(PHASE3_LAST_SYMBOL)
            if phase3_bp.GetNumLocations() < 1:
                raise RuntimeError(
                    f"failed to set breakpoint on {PHASE3_LAST_SYMBOL}; "
                    "build the native runner with debug symbols"
                )
            self._install_auto_callback(phase3_bp)

            self.compat_bp_id = compat_bp.GetID()
            self.phase3_bp_id = phase3_bp.GetID()

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
                if (self.expected_frames and
                        len(self.events) >= self.expected_frames):
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
                        f"native LLDB trace timed out after {self.timeout:.1f}s "
                        f"with {len(self.events)} event(s)"
                    )

                cont_error = process.Continue()
                if not cont_error.Success():
                    raise RuntimeError(
                        f"LLDB continue failed: {cont_error.GetCString()}"
                    )

            if self.expected_frames and len(self.events) < self.expected_frames:
                raise RuntimeError(
                    f"native LLDB trace captured only {len(self.events)} event(s); "
                    f"expected at least {self.expected_frames}"
                )
            return self.events
        finally:
            ACTIVE_TRACER = None
            lldb.SBDebugger.Destroy(debugger)

    def _install_auto_callback(self, breakpoint) -> None:
        error = breakpoint.SetScriptCallbackBody(
            "import __main__\n"
            "return __main__._native_lldb_motion_trace_callback("
            "frame, bp_loc, internal_dict)"
        )
        if not error.Success():
            raise RuntimeError(
                f"failed to install LLDB breakpoint callback: "
                f"{error.GetCString()}")
        breakpoint.SetAutoContinue(True)

    def handle_breakpoint_callback(self, breakpoint_id: int, frame) -> None:
        try:
            if breakpoint_id == self.compat_bp_id:
                self._on_progress_enter(frame)
            elif breakpoint_id == self.phase3_bp_id:
                self._on_phase3_last_enter(frame)
            elif breakpoint_id in self.progress_return_records:
                self._on_progress_return(breakpoint_id, frame)
            elif breakpoint_id in self.phase3_return_records:
                self._on_phase3_last_return(breakpoint_id, frame)
        except Exception as exc:
            self.callback_errors.append(str(exc))

    def _on_progress_enter(self, frame) -> None:
        objthis = ptr_to_hex(sb_unsigned(frame.FindRegister("x3")))
        lr = sb_unsigned(frame.FindRegister("lr"))
        if not lr:
            raise RuntimeError("progressCompat breakpoint had no LR register")

        record: dict[str, Any] = {
            "objthis": objthis,
            "players": [],
            "errors": [],
        }
        target = frame.GetThread().GetProcess().GetTarget()
        ret_bp = target.BreakpointCreateByAddress(lr)
        ret_bp.SetOneShot(True)
        try:
            ret_bp.SetThreadID(frame.GetThread().GetThreadID())
        except Exception:
            pass
        self._install_auto_callback(ret_bp)
        self.progress_return_records[ret_bp.GetID()] = record
        self.record_stack.append(record)
        self.current_record = record

    def _on_phase3_last_enter(self, frame) -> None:
        if self.current_record is None:
            return
        this_value = frame.FindVariable("this")
        player = ptr_to_hex(sb_unsigned(this_value))
        if player is None:
            player = ptr_to_hex(sb_unsigned(frame.FindRegister("x0")))
        lr = sb_unsigned(frame.FindRegister("lr"))
        if not lr:
            raise RuntimeError(f"{PHASE3_LAST_SYMBOL} breakpoint had no LR")
        if player is None:
            raise RuntimeError(f"{PHASE3_LAST_SYMBOL} breakpoint had no Player*")

        target = frame.GetThread().GetProcess().GetTarget()
        ret_bp = target.BreakpointCreateByAddress(lr)
        ret_bp.SetOneShot(True)
        try:
            ret_bp.SetThreadID(frame.GetThread().GetThreadID())
        except Exception:
            pass
        self._install_auto_callback(ret_bp)
        self.phase3_return_records[ret_bp.GetID()] = {
            "record": self.current_record,
            "player": player,
        }

    def _on_phase3_last_return(self, breakpoint_id: int, frame) -> None:
        info = self.phase3_return_records.pop(breakpoint_id, None)
        target = frame.GetThread().GetProcess().GetTarget()
        target.BreakpointDelete(breakpoint_id)
        if info is None:
            return

        record = info["record"]
        player = info["player"]
        try:
            layers = self._dump_layers(frame, player)
            record["players"].append({
                "ptr": player,
                "layers": layers,
            })
        except Exception as exc:
            record["errors"].append(str(exc))

    def _on_progress_return(self, breakpoint_id: int, frame) -> None:
        record = self.progress_return_records.pop(breakpoint_id, None)
        target = frame.GetThread().GetProcess().GetTarget()
        target.BreakpointDelete(breakpoint_id)
        if record is None:
            return

        flat_layers: list[dict[str, Any]] = []
        players = record.get("players") or []
        for player in players:
            for layer in player["layers"]:
                out = dict(layer)
                out["index"] = len(flat_layers)
                if player.get("ptr"):
                    out["sourcePlayer"] = player["ptr"]
                flat_layers.append(out)

        event = {
            "frameId": self.frame_counter,
            "objthis": record.get("objthis"),
            "topPlayer": players[0]["ptr"] if players else None,
            "playerCount": len(players),
            "layout": "native-lldb",
            "layers": flat_layers,
        }
        errors = record.get("errors") or []
        if errors:
            event["error"] = "; ".join(errors)
        self.events.append(event)
        self.frame_counter += 1
        if self.record_stack and self.record_stack[-1] is record:
            self.record_stack.pop()
        else:
            self.record_stack = [r for r in self.record_stack if r is not record]
        self.current_record = self.record_stack[-1] if self.record_stack else None

    def _dump_layers(self, frame, player_ptr_hex: str | None = None) -> list[dict[str, Any]]:
        this_value = frame.FindVariable("this")
        if this_value.IsValid() and sb_unsigned(this_value):
            player = this_value.Dereference()
        elif player_ptr_hex:
            player = self._player_value_from_ptr(frame, player_ptr_hex)
        else:
            raise RuntimeError("phase3 return frame has no `this` variable")
        if not player_ptr_hex:
            player_ptr_hex = ptr_to_hex(sb_unsigned(player.AddressOf()))
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
        return target.CreateValueFromAddress(
            "node", target.ResolveLoadAddress(addr), node_type)

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
            "label": "",
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
            "blendMode": sb_signed(sb_child_optional(node, "stencilType"), 0),
            "currentImage": "",
        }

    def _player_value_from_ptr(self, frame, player_ptr_hex: str):
        target = frame.GetThread().GetProcess().GetTarget()
        addr = int(player_ptr_hex, 16)
        player_type = target.FindFirstType("motion::Player")
        if player_type.IsValid():
            try:
                value = target.CreateValueFromAddress(
                    "player", addr, player_type)
                if value.IsValid():
                    return value
            except Exception:
                pass
        value = frame.EvaluateExpression(
            f"reinterpret_cast<motion::Player *>({player_ptr_hex})")
        if value.IsValid() and value.GetError().Success():
            return value.Dereference()
        err = value.GetError().GetCString() if value.IsValid() else "invalid value"
        raise RuntimeError(
            f"could not materialize motion::Player at {player_ptr_hex}: {err}")

    def _runtime_value_for_player_ptr(self, frame, player_ptr_hex: str):
        player = self._player_value_from_ptr(frame, player_ptr_hex)
        runtime_shared = sb_child(player, "_runtime", 0)
        runtime_ptr = sb_child(runtime_shared, "pointer", 0)
        if not sb_unsigned(runtime_ptr):
            raise RuntimeError("Player::_runtime pointer is null")
        return runtime_ptr.Dereference()

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
        print("native LLDB motion trace is only supported on macOS",
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
        tracer = NativeMotionTracer(
            lldb=lldb,
            runner=runner,
            startup_xp3=startup_xp3,
            repo_root=repo_root,
            expected_frames=args.expected_frames,
            timeout=args.timeout,
        )
        events = tracer.run()
        trace_out.parent.mkdir(parents=True, exist_ok=True)
        trace_out.write_text(
            json.dumps(events, ensure_ascii=False, allow_nan=False) + "\n",
            encoding="utf-8",
        )
    except Exception as exc:
        print(f"native LLDB trace failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
