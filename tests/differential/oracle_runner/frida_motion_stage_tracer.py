"""Frida tracer for staged motion_playback Android oracle diagnostics."""

from __future__ import annotations

import json
import hashlib
import time
from pathlib import Path
from typing import Any, Sequence

from oracle_runner.png_artifacts import bgra_rgba_sha256_bytes

try:
    import frida  # type: ignore
except ModuleNotFoundError:  # pragma: no cover - raised at attach time
    frida = None  # noqa: N816


_AGENT_PATH = Path(__file__).with_name("frida_motion_stage_agent.js")
_FRAME_SELECTION_PROJECTION_PATH = (
    Path(__file__).resolve().parents[1]
    / "motion_stage_projections" / "frame_selection_v1.json"
)

STAGES: tuple[str, ...] = (
    "static_parse",
    "init_motion",
    "variable_binding",
    "frame_selection",
    "sub_motion_decision",
    "trace_flatten",
)

RENDER_STAGES: tuple[str, ...] = (
    "draw_dispatch",
    "render_prepare",
    "render_commands",
    "render_execute",
    "layer_save",
    "layer_raw_probe",
    "layer_visual_readback",
)


def _load_agent_source() -> str:
    source = _AGENT_PATH.read_text(encoding="utf-8")
    projection = json.loads(
        _FRAME_SELECTION_PROJECTION_PATH.read_text(encoding="utf-8")
    )
    return source.replace(
        "__FRAME_SELECTION_PROJECTION_JSON__",
        json.dumps(projection, separators=(",", ":")),
    )


class FridaMotionStageTracer:
    """Installs the staged motion diagnostic agent in the APK harness."""

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
        self._info: dict[str, Any] | None = None
        self._image_checkpoint_dir: Path | None = None
        self._image_checkpoints: list[dict[str, Any]] = []
        self._layer_raw_probe_dir: Path | None = None
        self._layer_raw_probe_updates: dict[int, dict[str, Any]] = {}
        self._layer_visual_readback_updates: dict[int, dict[str, Any]] = {}

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
                    "to /data/local/tmp/frida-server and start it as root"
                ) from exc
            except frida.ProcessNotFoundError as exc:
                last_err = exc
                time.sleep(0.2)
        else:
            raise RuntimeError(
                f"frida attach(pid={pid}) timed out: {last_err!r}"
            )

        source = _load_agent_source()
        self._script = self._session.create_script(source)
        self._script.on("message", self._on_message)
        self._script.load()
        self._api = self._script.exports_sync
        self._info = dict(self._api.setup())

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

    def __enter__(self) -> "FridaMotionStageTracer":
        self.attach()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.detach()

    @property
    def info(self) -> dict[str, Any] | None:
        return self._info

    def configure_image_checkpoints(self, raw_dir: Path | None) -> None:
        self._image_checkpoint_dir = raw_dir
        self._image_checkpoints = []
        self._layer_raw_probe_dir = (
            raw_dir.parent / ".oracle_layer_raw_probe"
            if raw_dir is not None else None
        )
        self._layer_raw_probe_updates = {}
        self._layer_visual_readback_updates = {}
        if raw_dir is not None:
            raw_dir.mkdir(parents=True, exist_ok=True)
        if self._layer_raw_probe_dir is not None:
            self._layer_raw_probe_dir.mkdir(parents=True, exist_ok=True)

    def image_checkpoints(self) -> list[dict[str, Any]]:
        return [dict(item) for item in self._image_checkpoints]

    def start_record(
        self,
        stages: Sequence[str],
        *,
        record_render_step_checkpoints: bool = False,
        record_layer_raw_probes: bool = False,
        record_save_layer_visual_readback_probes: bool = False,
        save_layer_visual_readback_frame_start: int = 0,
        save_layer_visual_readback_frame_count: int = 1,
        capture_frame_start: int = 0,
        capture_frame_count: int = -1,
        render_case_frame_bases: dict[str, int] | None = None,
    ) -> None:
        if self._api is None:
            raise RuntimeError("tracer not attached; call attach() first")
        self._api.start_record(list(stages), {
            "recordRenderStepCheckpoints": bool(
                record_render_step_checkpoints),
            "recordLayerRawProbes": bool(record_layer_raw_probes),
            "recordSaveLayerVisualReadbackProbes": bool(
                record_save_layer_visual_readback_probes),
            "saveLayerVisualReadbackFrameStart": int(
                save_layer_visual_readback_frame_start),
            "saveLayerVisualReadbackFrameCount": int(
                save_layer_visual_readback_frame_count),
            "captureFrameStart": int(capture_frame_start),
            "captureFrameCount": int(capture_frame_count),
            "renderCaseFrameBases": {
                str(case_id): int(frame_base)
                for case_id, frame_base in (
                    render_case_frame_bases or {}).items()
            },
        })

    def stop_record(self) -> list[dict[str, Any]]:
        if self._api is None:
            return []
        raw = self._api.stop_record()
        events = list(raw or [])
        for ev in events:
            seq = ev.get("seq")
            if not isinstance(seq, int):
                continue
            if ev.get("stage") == "layer_raw_probe":
                update = self._layer_raw_probe_updates.get(seq)
                if update:
                    ev.update(update)
            elif ev.get("stage") == "layer_visual_readback":
                update = self._layer_visual_readback_updates.get(seq)
                if update:
                    ev.update(update)
        return events

    def event_count(self) -> int:
        if self._api is None:
            return 0
        return int(self._api.event_count())

    def raw_event_count(self) -> int:
        if self._api is None:
            return 0
        return int(self._api.raw_event_count())

    def _get_device(self):
        mgr = frida.get_device_manager()
        if self._device_id:
            return mgr.get_device(self._device_id, timeout=self._attach_timeout)
        return frida.get_usb_device(timeout=self._attach_timeout)

    def _resolve_pid(self) -> int:
        if getattr(self._adb, "pid", 0):
            return self._adb.pid
        raise RuntimeError(
            "AdbHarnessEngine has no pid - call engine.start() first"
        )

    def _on_message(self, message, data) -> None:
        import sys

        if message.get("type") == "error":
            print(
                f"[frida-motion-stage-agent] "
                f"{message.get('stack') or message}",
                file=sys.stderr,
            )
            return
        if message.get("type") != "send":
            return
        payload = message.get("payload")
        if not isinstance(payload, dict):
            return
        payload_type = payload.get("type")
        if payload_type == "layer_raw_probe":
            self._handle_layer_raw_probe_message(payload, data)
            return
        if payload_type == "layer_visual_readback_probe":
            self._handle_layer_visual_readback_message(payload, data)
            return
        if payload_type != "render_image_checkpoint":
            return
        record = dict(payload)
        record.pop("type", None)
        if record.get("ok") and data is not None:
            raw_dir = self._image_checkpoint_dir
            if raw_dir is None:
                record["ok"] = False
                record["error"] = "host raw checkpoint directory is not configured"
            else:
                frame_value = record.get("frameId")
                if not isinstance(frame_value, int):
                    record["ok"] = False
                    record["error"] = "checkpoint has no integer frameId"
                    self._image_checkpoints.append(record)
                    return
                frame_id = int(frame_value)
                phase = str(record.get("phase") or "unknown")
                pixel_format = str(record.get("pixelFormat") or "bgra32")
                suffix = (
                    "rgba" if pixel_format.startswith("rgba32")
                    else "bgra"
                )
                raw_path = raw_dir / f"frame_{frame_id:04d}_{phase}.{suffix}"
                raw_path.write_bytes(bytes(data))
                record["rawPath"] = str(raw_path)
        self._image_checkpoints.append(record)

    def _handle_layer_raw_probe_message(self, payload, data) -> None:
        record = dict(payload)
        record.pop("type", None)
        seq = record.get("seq")
        if not isinstance(seq, int):
            return
        update: dict[str, Any] = {"ok": bool(record.get("ok"))}
        if record.get("ok") and data is not None:
            raw_bytes = bytes(data)
            update["bytes"] = len(raw_bytes)
            try:
                update["rgbaSha256"] = bgra_rgba_sha256_bytes(raw_bytes)
            except RuntimeError as exc:
                update["ok"] = False
                update["error"] = str(exc)
        elif not record.get("ok"):
            update["error"] = record.get("error") or "snapshot failed"
        self._layer_raw_probe_updates[seq] = update

    def _handle_layer_visual_readback_message(self, payload, data) -> None:
        record = dict(payload)
        record.pop("type", None)
        seq = record.get("seq")
        if not isinstance(seq, int):
            return
        update: dict[str, Any] = {"ok": bool(record.get("ok"))}
        if record.get("ok") and data is not None:
            raw_bytes = bytes(data)
            update["bytes"] = len(raw_bytes)
            update["rowSha256"] = hashlib.sha256(raw_bytes).hexdigest()
            update["rowPrefixHex"] = raw_bytes[:64].hex()
            update["rowSuffixHex"] = raw_bytes[-64:].hex()
        elif not record.get("ok"):
            update["error"] = record.get("error") or "snapshot failed"
        self._layer_visual_readback_updates[seq] = update
