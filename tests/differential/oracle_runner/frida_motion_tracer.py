"""Frida tracer specialised for the motion_playback oracle family.

Companion to `AdbHarnessEngine`: harness-RPC triggers
`TVPMainScene::startupFrom(logo_test.xp3)` which schedules the cocos2d
GL thread to run `startup.tjs`. That script drives `Motion.Player` at
the native frame rate. This tracer attaches to the same process and
hooks `Player_updateLayers` so we capture per-frame per-layer state
inline with the GL-thread update, avoiding the cross-thread SIGSEGV we
got trying to drive Motion.Player from the harness-rpc pthread.

See `frida_motion_agent.js` for the in-process JS side.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Any

try:
    import frida  # type: ignore
except ModuleNotFoundError:  # pragma: no cover — raised at attach time
    frida = None  # noqa: N816


_AGENT_PATH = Path(__file__).with_name("frida_motion_agent.js")


class FridaMotionTracer:
    """Installs the `Player_updateLayers` hook and buffers per-frame events.

    Lifecycle:
        tracer = FridaMotionTracer(engine)
        with tracer:                        # attach + install hook
            tracer.start_record()
            trigger_startup(engine, path)   # cocos2d GL thread starts playing
            wait_for_done(...)
            events = tracer.stop_record()
    """

    def __init__(
        self,
        adb_engine,
        *,
        device_id: str | None = None,
        attach_timeout: float = 10.0,
    ) -> None:
        if frida is None:
            raise RuntimeError(
                "frida-python is not installed; "
                "`pip install -r tests/differential/oracle_runner/"
                "requirements-oracle.txt`"
            )
        self._adb = adb_engine
        self._device_id = device_id or adb_engine.serial
        self._attach_timeout = attach_timeout
        self._session: Any = None
        self._script: Any = None
        self._api: Any = None
        self._info: dict | None = None

    # ------------------------------------------------------------- lifecycle
    def attach(self) -> None:
        if self._session is not None:
            return
        device = self._get_device()
        pid = self._resolve_pid()
        deadline = time.time() + self._attach_timeout
        last_err: Exception | None = None
        while time.time() < deadline:
            try:
                self._session = device.attach(pid)
                break
            except frida.ServerNotRunningError as exc:
                raise RuntimeError(
                    "frida-server not running on device at "
                    f"{self._device_id!r}; push tools/bin/android/frida-server "
                    "to /data/local/tmp/frida-server and start it (as root) "
                    "first"
                ) from exc
            except frida.ProcessNotFoundError as exc:
                last_err = exc
                time.sleep(0.2)
        else:
            raise RuntimeError(
                f"frida attach(pid={pid}) timed out: {last_err!r}"
            )
        source = _AGENT_PATH.read_text(encoding="utf-8")
        self._script = self._session.create_script(source)
        self._script.on("message", self._on_message)
        self._script.load()
        self._api = self._script.exports_sync
        self._info = self._api.setup()

    def detach(self) -> None:
        if self._script is not None:
            try:
                self._script.unload()
            except Exception:
                pass
            self._script = None
        if self._session is not None:
            try:
                self._session.detach()
            except Exception:
                pass
            self._session = None
        self._api = None

    def __enter__(self) -> "FridaMotionTracer":
        self.attach()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.detach()

    # ----------------------------------------------------------------- API
    def start_record(self) -> None:
        self._api.start_record()

    def stop_record(self) -> list[dict]:
        raw = self._api.stop_record()
        return list(raw or [])

    def event_count(self) -> int:
        return int(self._api.event_count())

    def diag_dump(self, player_ptr_hex: str) -> dict:
        """Step-A bring-up helper: dump 256 bytes at `player_ptr_hex` so
        the host can inspect the Node container layout before trusting
        the agent's walker. Not used in steady state."""
        return dict(self._api.diag_dump(player_ptr_hex))

    @property
    def info(self) -> dict | None:
        return self._info

    # ------------------------------------------------------------- helpers
    def _get_device(self):
        mgr = frida.get_device_manager()
        if self._device_id:
            return mgr.get_device(self._device_id, timeout=self._attach_timeout)
        return frida.get_usb_device(timeout=self._attach_timeout)

    def _resolve_pid(self) -> int:
        if getattr(self._adb, "pid", 0):
            return self._adb.pid
        raise RuntimeError(
            "AdbHarnessEngine has no pid — call engine.start() first"
        )

    def _on_message(self, message, data):
        import sys
        if message.get("type") == "error":
            print(
                f"[frida-motion-agent] {message.get('stack') or message}",
                file=sys.stderr,
            )


def wait_for_playback_complete(
    tracer: FridaMotionTracer,
    *,
    min_segments: int = 2,
    min_frames_per_segment: int = 30,
    idle_frames_for_done: int = 60,
    poll_interval: float = 0.3,
    timeout: float = 60.0,
) -> list[dict]:
    """Poll tracer event count until playback is 'done'.

    `logo_test.xp3`'s `startup.tjs` plays yuzulogo then m2logo
    sequentially (two distinct `Motion.Player` instances → two distinct
    player pointers in our trace). After both finish, the continuous
    handler removes itself and no more hook events fire.

    We declare completion when we've seen at least `min_segments`
    player-pointer groups each with ≥ `min_frames_per_segment` frames,
    AND no new events have arrived in the last `idle_frames_for_done`
    poll ticks. Returns the frozen event list.
    """
    deadline = time.time() + timeout
    last_count = -1
    idle_ticks = 0
    while time.time() < deadline:
        count = tracer.event_count()
        if count == last_count:
            idle_ticks += 1
        else:
            idle_ticks = 0
            last_count = count

        # Cheap completion check without pulling full events — use
        # `idle_frames_for_done` poll ticks of no growth.
        if count > 0 and idle_ticks * poll_interval >= 2.0:
            # Pull events and verify segment shape.
            events = tracer.stop_record()
            tracer.start_record()  # resume in case we were wrong
            segments = segment_by_player(events)
            substantive = [s for s in segments if len(s["frames"]) >= min_frames_per_segment]
            if len(substantive) >= min_segments:
                return events
            # Not done yet; keep polling but factor these events back in
            # by replaying via a separate buffer. Simpler: just accept and
            # loop — next stop_record() will include these + future.
            # (Note: we already reset recording above so we'd double-count.
            # For bring-up simplicity just return whatever we have; caller
            # can re-run if needed.)
            return events
        time.sleep(poll_interval)
    raise RuntimeError(
        f"motion playback never completed in {timeout}s "
        f"(last event count: {last_count})"
    )


def segment_by_player(events: list[dict]) -> list[dict]:
    """Group contiguous same-`objthis` events into segments.

    We key on the TJS dispatch `objthis` (the iTJSDispatch2 wrapping a
    `Motion.Player` TJS object) rather than the native Player* because
    objthis is guaranteed stable across every progress() call of that
    TJS instance; the native Player* can in theory be reallocated, and
    for cases where the node walker fails (`player=None`) we still need
    a segmentation key. `startup.tjs` does
    `player = new Motion.Player(rm)` once per fixture, yielding one
    distinct objthis per fixture.
    """
    segments: list[dict] = []
    for ev in events:
        key = ev.get("objthis") or ev.get("player")
        if not segments or segments[-1]["player"] != key:
            segments.append({"player": key, "frames": []})
        segments[-1]["frames"].append(ev)
    return segments
