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
PROGRESS_BRIDGE_SYMBOL = "motion::Player::progressFrames_guess"
DELIVER_CONTINUOUS_SYMBOL = "_TVPDeliverContinuousEvent"
ACTIVE_TRACER: "NativeMotionTracer | None" = None
LLDB_INVALID_ADDRESS = (1 << 64) - 1
SIMULATION_FPS = 15.0


def _native_lldb_motion_trace_callback(frame, bp_loc, _internal_dict):
    if ACTIVE_TRACER is None:
        return False
    breakpoint_id = bp_loc.GetBreakpoint().GetID()
    ACTIVE_TRACER.handle_breakpoint_callback(breakpoint_id, frame)
    return ACTIVE_TRACER.should_stop()


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
                   help="Path to the deterministic 15 Hz oracle XP3")
    p.add_argument("--trace-out", required=True,
                   help="Path to write JSON trace events")
    p.add_argument("--expected-frames", type=int, required=True,
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


def callee_return_address(frame) -> int | None:
    """Return the caller PC without assuming AArch64's LR register.

    The native differential runner is x86_64 on Intel macOS, while the first
    tracer implementation was copied from the AArch64 oracle ABI and read only
    `lr`.  LLDB's caller frame is architecture-neutral and is also what the
    staged native tracer already uses.
    """

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
    for register_name in ("x30", "lr"):
        value = sb_unsigned(frame.FindRegister(register_name))
        if value:
            return value
    return None


def pointer_argument(frame, variable_name: str,
                     register_names: tuple[str, ...]) -> int | None:
    value = frame.FindVariable(variable_name)
    if value and value.IsValid():
        pointer = sb_unsigned(value)
        if pointer:
            return pointer
    for register_name in register_names:
        pointer = sb_unsigned(frame.FindRegister(register_name))
        if pointer:
            return pointer
    return None


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
        self.progress_bridge_bp_id: int | None = None
        self.tick_bp_id: int | None = None
        self.deliver_bp_id: int | None = None
        self.record_stack: list[dict[str, Any]] = []
        self.progress_return_records: dict[int, dict[str, Any]] = {}
        self.phase3_return_records: dict[int, dict[str, Any]] = {}
        self.tick_return_records: set[int] = set()
        self.delivery_return_records: set[int] = set()
        self.delivery_active = False
        self.delivery_extra_record: dict[str, Any] | None = None
        self.delivery_candidates: list[dict[str, Any]] = []
        self.callback_errors: list[str] = []
        self.node_layout: dict[str, int] | None = None
        self.start_monotonic = 0.0
        self.timed_out = False
        self.virtual_tick_base: int | None = None
        self.virtual_tick_index = 0
        self.last_virtual_tick: int | None = None

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

            tick_bp = target.BreakpointCreateByName("TVPGetTickCount")
            if tick_bp.GetNumLocations() < 1:
                raise RuntimeError(
                    "failed to set breakpoint on TVPGetTickCount; build the "
                    "native runner with debug symbols")
            self._install_auto_callback(tick_bp)

            deliver_bp = target.BreakpointCreateByName(
                DELIVER_CONTINUOUS_SYMBOL)
            if deliver_bp.GetNumLocations() != 1:
                raise RuntimeError(
                    f"expected one breakpoint location for "
                    f"{DELIVER_CONTINUOUS_SYMBOL}, got "
                    f"{deliver_bp.GetNumLocations()}")
            self._install_auto_callback(deliver_bp)

            progress_bridge_bp = target.BreakpointCreateByName(
                PROGRESS_BRIDGE_SYMBOL)
            if progress_bridge_bp.GetNumLocations() < 1:
                raise RuntimeError(
                    f"failed to set breakpoint on {PROGRESS_BRIDGE_SYMBOL}; "
                    "build the native runner with debug symbols")
            self._install_auto_callback(progress_bridge_bp)

            self.compat_bp_id = compat_bp.GetID()
            self.phase3_bp_id = phase3_bp.GetID()
            self.progress_bridge_bp_id = progress_bridge_bp.GetID()
            self.tick_bp_id = tick_bp.GetID()
            self.deliver_bp_id = deliver_bp.GetID()

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
                if self.timed_out:
                    process.Kill()
                    raise RuntimeError(
                        f"native LLDB trace timed out after {self.timeout:.1f}s "
                        f"with {len(self.events)} event(s)"
                    )
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

    def should_stop(self) -> bool:
        """Let synchronous SBTarget.Launch return at a terminal condition.

        Script callbacks normally return ``False`` so LLDB continues the
        inferior.  The old unconditional AutoContinue kept Launch blocked even
        after the requested frame count, which also made the Python-side
        timeout loop unreachable.
        """

        if self.callback_errors:
            return True
        if self.expected_frames and len(self.events) >= self.expected_frames:
            return True
        if (self.start_monotonic and
                time.monotonic() - self.start_monotonic >= self.timeout):
            self.timed_out = True
            return True
        return False

    def handle_breakpoint_callback(self, breakpoint_id: int, frame) -> None:
        try:
            if breakpoint_id == self.compat_bp_id:
                self._on_progress_enter(frame)
            elif breakpoint_id == self.phase3_bp_id:
                self._on_phase3_last_enter(frame)
            elif breakpoint_id == self.progress_bridge_bp_id:
                self._on_progress_bridge_enter(frame)
            elif breakpoint_id == self.tick_bp_id:
                self._on_tick_enter(frame)
            elif breakpoint_id == self.deliver_bp_id:
                self._on_delivery_enter(frame)
            elif breakpoint_id in self.progress_return_records:
                self._on_progress_return(breakpoint_id, frame)
            elif breakpoint_id in self.phase3_return_records:
                self._on_phase3_last_return(breakpoint_id, frame)
            elif breakpoint_id in self.tick_return_records:
                self._on_tick_return(breakpoint_id, frame)
            elif breakpoint_id in self.delivery_return_records:
                self._on_delivery_return(breakpoint_id, frame)
        except Exception as exc:
            self.callback_errors.append(str(exc))

    def _on_progress_enter(self, frame) -> None:
        objthis = ptr_to_hex(pointer_argument(
            frame, "objthis", ("x3", "rcx")))
        return_address = callee_return_address(frame)
        if not return_address:
            raise RuntimeError(
                "progressCompat breakpoint had no callee return address")

        record: dict[str, Any] = {
            "objthis": objthis,
            "players": [],
            "errors": [],
        }
        if self.delivery_extra_record is not None and \
                self.delivery_extra_record.get("objthis") is None:
            self.delivery_extra_record["objthis"] = objthis
        target = frame.GetThread().GetProcess().GetTarget()
        ret_bp = target.BreakpointCreateByAddress(return_address)
        ret_bp.SetOneShot(True)
        try:
            ret_bp.SetThreadID(frame.GetThread().GetThreadID())
        except Exception:
            pass
        self._install_auto_callback(ret_bp)
        self.progress_return_records[ret_bp.GetID()] = record
        self.record_stack.append(record)
        self.current_record = record

    @staticmethod
    def _new_delivery_record() -> dict[str, Any]:
        return {"objthis": None, "players": [], "errors": []}

    def _on_delivery_enter(self, frame) -> None:
        if self.delivery_active:
            raise RuntimeError("nested continuous delivery in native tracer")
        self.delivery_active = True
        self.delivery_extra_record = self._new_delivery_record()
        self.delivery_candidates = []
        self.record_stack.clear()
        self.current_record = self.delivery_extra_record

        return_address = callee_return_address(frame)
        if not return_address:
            raise RuntimeError(
                "continuous delivery breakpoint had no return address")
        thread = frame.GetThread()
        target = thread.GetProcess().GetTarget()
        ret_bp = target.BreakpointCreateByAddress(return_address)
        ret_bp.SetOneShot(True)
        try:
            ret_bp.SetThreadID(thread.GetThreadID())
        except Exception:
            pass
        self._install_auto_callback(ret_bp)
        self.delivery_return_records.add(ret_bp.GetID())

    def _on_delivery_return(self, breakpoint_id: int, frame) -> None:
        self.delivery_return_records.discard(breakpoint_id)
        target = frame.GetThread().GetProcess().GetTarget()
        target.BreakpointDelete(breakpoint_id)

        extra = self.delivery_extra_record
        if extra is not None and (extra.get("players") or extra.get("errors")):
            self.delivery_candidates.append(self._event_from_record(extra))

        if self.delivery_candidates:
            # A delivery can retire one old Player and publish/progress the new
            # active Player tree before returning.  Android records the complete
            # active tree at the delivery boundary; prefer the candidate with
            # the most nested Players, then the most flattened layers.
            _candidate_index, event = max(
                enumerate(self.delivery_candidates),
                key=lambda indexed: (
                    int(indexed[1].get("playerCount") or 0),
                    len(indexed[1].get("layers") or []),
                    indexed[0],
                ),
            )
            self._commit_event(event)
        elif self.virtual_tick_index > 0:
            # A delivery aborted before any motion handler ran.  It must not
            # consume one 15 Hz oracle slot; repeat this integer-grid tick on
            # the next delivery that actually produces a sample.
            self.virtual_tick_index -= 1

        self.delivery_active = False
        self.delivery_extra_record = None
        self.delivery_candidates = []
        self.record_stack.clear()
        self.current_record = None

    def _on_progress_bridge_enter(self, frame) -> None:
        if self.current_record is None:
            return
        player = ptr_to_hex(pointer_argument(
            frame, "this", ("x0", "rdi")))
        if player is not None:
            self.current_record["fallbackPlayer"] = player

    def _on_tick_enter(self, frame) -> None:
        """Virtualize only _TVPDeliverContinuousEvent's tick call edge.

        This mirrors the Android Frida oracle exactly: startup.tjs still owns
        ``tick - lastTick`` and every caller outside continuous delivery keeps
        the real clock.  Virtualizing the later Player argument was subtly too
        late because AffineSourceMotion also consumes ``_interval`` in TJS.
        """

        thread = frame.GetThread()
        if thread.GetNumFrames() < 2:
            return
        caller = thread.GetFrameAtIndex(1)
        caller_name = caller.GetFunctionName() or ""
        if "TVPDeliverContinuousEvent" not in caller_name:
            return
        return_address = callee_return_address(frame)
        if not return_address:
            raise RuntimeError(
                "TVPGetTickCount continuous call had no return address")
        target = thread.GetProcess().GetTarget()
        ret_bp = target.BreakpointCreateByAddress(return_address)
        ret_bp.SetOneShot(True)
        try:
            ret_bp.SetThreadID(thread.GetThreadID())
        except Exception:
            pass
        self._install_auto_callback(ret_bp)
        self.tick_return_records.add(ret_bp.GetID())

    def _on_tick_return(self, breakpoint_id: int, frame) -> None:
        self.tick_return_records.discard(breakpoint_id)
        target = frame.GetThread().GetProcess().GetTarget()
        target.BreakpointDelete(breakpoint_id)
        result_register = None
        for name in ("rax", "x0"):
            register = frame.FindRegister(name)
            if register and register.IsValid():
                result_register = register
                break
        if result_register is None:
            raise RuntimeError(
                "TVPGetTickCount return breakpoint had no result register")
        actual = sb_unsigned(result_register)
        if self.virtual_tick_base is None:
            self.virtual_tick_base = actual
        first_grid_tick = int(1000.0 / SIMULATION_FPS)
        grid_tick = int(
            (self.virtual_tick_index + 1) * 1000.0 / SIMULATION_FPS)
        virtual_tick = self.virtual_tick_base + grid_tick - first_grid_tick
        if not result_register.SetValueFromCString(str(virtual_tick)):
            raise RuntimeError("failed to replace continuous-handler tick")
        self.last_virtual_tick = virtual_tick
        self.virtual_tick_index += 1

    def _on_phase3_last_enter(self, frame) -> None:
        if self.current_record is None:
            return
        this_value = frame.FindVariable("this")
        player = ptr_to_hex(sb_unsigned(this_value))
        if player is None:
            player = ptr_to_hex(pointer_argument(
                frame, "this", ("x0", "rdi")))
        return_address = callee_return_address(frame)
        if not return_address:
            raise RuntimeError(
                f"{PHASE3_LAST_SYMBOL} breakpoint had no callee return address")
        if player is None:
            raise RuntimeError(f"{PHASE3_LAST_SYMBOL} breakpoint had no Player*")

        target = frame.GetThread().GetProcess().GetTarget()
        ret_bp = target.BreakpointCreateByAddress(return_address)
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

        if not record.get("players") and record.get("fallbackPlayer"):
            player = record["fallbackPlayer"]
            try:
                record["players"].append({
                    "ptr": player,
                    "layers": self._dump_layers(frame, player),
                    "layout": "native-lldb-fallback",
                })
            except Exception as exc:
                record["errors"].append(str(exc))

        event = self._event_from_record(record)
        if self.delivery_active:
            self.delivery_candidates.append(event)
        else:
            self._commit_event(event)

        if self.record_stack and self.record_stack[-1] is record:
            self.record_stack.pop()
        else:
            self.record_stack = [r for r in self.record_stack if r is not record]
        if self.record_stack:
            self.current_record = self.record_stack[-1]
        elif self.delivery_active:
            self.current_record = self.delivery_extra_record
        else:
            self.current_record = None

    def _event_from_record(self, record: dict[str, Any]) -> dict[str, Any]:
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
            "frameId": -1,
            "objthis": record.get("objthis"),
            "topPlayer": players[0]["ptr"] if players else None,
            "playerCount": len(players),
            "layout": "native-lldb",
            "virtualTick": self.last_virtual_tick,
            "layers": flat_layers,
        }
        errors = record.get("errors") or []
        if errors:
            event["error"] = "; ".join(errors)
        return event

    def _commit_event(self, event: dict[str, Any]) -> None:
        event["frameId"] = self.frame_counter
        self.events.append(event)
        self.frame_counter += 1
        if len(self.events) == 1 or len(self.events) % 5 == 0:
            print(
                f"native LLDB trace: captured {len(self.events)}/"
                f"{self.expected_frames} frame(s)",
                file=sys.stderr,
                flush=True,
            )

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
            layers.append(self._read_layer_from_node(frame, node, i))
        return layers

    def _node_count_for_player_ptr(self, frame, player_ptr_hex: str) -> int:
        _sequence, count = self._node_sequence_for_player_ptr(
            frame, player_ptr_hex)
        if count is not None:
            return count
        expr = (
            f"reinterpret_cast<motion::Player *>({player_ptr_hex})"
            "->nodes().size()"
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
            f"->nodes()[{index}]))"
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
            player = self._player_value_from_ptr(frame, player_ptr_hex)
            nodes = sb_child_optional(player, "_nodes", "nodes")
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

    def _read_layer_from_node(self, frame, node, index: int) -> dict[str, Any]:
        node_type = node.GetType().GetUnqualifiedType()
        layout = self._ensure_node_layout(node_type)
        address = int(node.GetLoadAddress())
        if not address or address == LLDB_INVALID_ADDRESS:
            address = sb_unsigned(node.AddressOf())
        if not address:
            raise RuntimeError(f"node {index} has no load address")
        error = self.lldb.SBError()
        blob = frame.GetThread().GetProcess().ReadMemory(
            address, layout["sizeof"], error)
        if not error.Success() or len(blob) != layout["sizeof"]:
            raise RuntimeError(
                f"failed to read node {index}: {error.GetCString()}")
        return {
            "index": index,
            "label": "",
            "nodeType": self._read_i32(blob, layout["nodeType"]),
            "visible": self._read_bool(blob, layout["visible"]),
            "active": self._read_bool(blob, layout["active"]),
            "flipX": self._read_bool(blob, layout["flipX"]),
            "flipY": self._read_bool(blob, layout["flipY"]),
            "posX": self._read_f64(blob, layout["posX"]),
            "posY": self._read_f64(blob, layout["posY"]),
            "posZ": self._read_f64(blob, layout["posZ"]),
            "angleDeg": self._read_f64(blob, layout["angle"]),
            "scaleX": self._read_f64(blob, layout["scaleX"]),
            "scaleY": self._read_f64(blob, layout["scaleY"]),
            "slantX": self._read_f64(blob, layout["slantX"]),
            "slantY": self._read_f64(blob, layout["slantY"]),
            "opacity": self._read_i32(blob, layout["opacity"]),
            "blendMode": self._read_i32(blob, layout["stencilType"]),
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
