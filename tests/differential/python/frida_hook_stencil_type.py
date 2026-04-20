#!/usr/bin/env python3
"""Observe runtime stencilType values at libkrkr2.so + 0x6BD958.

Purpose:
    Resolve the m2logo.mtn stencilType paradox. Static PSB parsing says
    all 31 back_white clip nodes have stencilType == 0, which should
    make `sub_6BD8DC @ 0x6BD958` (`LDR W2, [X1, #0x34]; CBZ W2, ...`)
    always fall through to the mask-fail branch — yet libkrkr2.so
    renders m2logo normally. Either the PSB decoder writes a non-zero
    value at runtime, or the field is rewritten per-frame, or our
    static analysis is wrong. This hook reads the real value.

Runs against the `org.github.krkr2` harness APK (HarnessActivity) that
the ADB oracle already provisions. We don't use the harness RPC — we
just need the app loaded with libkrkr2.so so that m2logo.mtn plays
naturally. Attach is by process name; PID is not needed.

Usage:
    # 1) Provision device per tests/differential/oracle_runner/README.md
    #    (push libs + frida-server + install krkr2-harness.apk). Ensure
    #    frida-server is running and the APK is launched to the point
    #    where m2logo.mtn is loaded (start a scene that uses it).
    # 2) Run this script on the host:
    python3 tests/differential/python/frida_hook_stencil_type.py \
        [--process com.eyagi.kirikiroid2|org.github.krkr2] \
        [--offset 0x6BD958] [--max-unique 200]

The script prints one line per unique MotionNode pointer observed and
Ctrl-C exits. See README at bottom for full provisioning walkthrough.
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import frida  # type: ignore
except ModuleNotFoundError:
    print("frida-python not installed. "
          "pip install -r tests/differential/oracle_runner/requirements-oracle.txt",
          file=sys.stderr)
    sys.exit(2)


# Default process candidates, in priority order. The repacked harness
# APK uses `org.github.krkr2`; the vanilla upstream uses
# `com.eyagi.kirikiroid2`. Either works — we just need libkrkr2.so
# loaded.
DEFAULT_PROCESS_CANDIDATES = (
    "org.github.krkr2",
    "com.eyagi.kirikiroid2",
)

DEFAULT_OFFSET = 0x6BD958  # LDR W2, [X1, #0x34] inside sub_6BD8DC


AGENT_JS = r"""
'use strict';

// Host -> agent config via rpc.exports.setup()
let HOOK_OFFSET = 0x6BD958;
let MAX_UNIQUE = 200;     // cap unique nodes logged
let DUMP_FIRST_N = 8;     // hex-dump node+0x30..0x50 for first N nodes

const seen = new Set();   // node pointer (string) -> observed once
let base = null;

function ensureBase() {
    if (base !== null) return base;
    base = Module.findBaseAddress('libkrkr2.so');
    if (base === null) {
        throw new Error('libkrkr2.so not loaded in target process');
    }
    send({ kind: 'base', base: base.toString() });
    return base;
}

function dumpNode(nodePtr) {
    // node+0x30 .. node+0x50 (32 bytes across stencilType/flags area).
    try {
        const bytes = nodePtr.add(0x30).readByteArray(0x20);
        return hexlify(bytes);
    } catch (e) {
        return `<read failed: ${e.message}>`;
    }
}

function hexlify(buf) {
    const u8 = new Uint8Array(buf);
    let s = '';
    for (let i = 0; i < u8.length; i++) {
        const b = u8[i];
        s += (b < 16 ? '0' : '') + b.toString(16);
        if ((i & 3) === 3 && i !== u8.length - 1) s += ' ';
    }
    return s;
}

function install() {
    const hookAddr = ensureBase().add(HOOK_OFFSET);
    Interceptor.attach(hookAddr, {
        onEnter(args) {
            // This is a MID-FUNCTION hook, not a function entry — but
            // Frida.Interceptor.attach works on any instruction. x1 at
            // this point is the MotionNode pointer we care about.
            try {
                const node = this.context.x1;
                if (node.isNull()) return;

                // stencilType field at node+0x34 (int32).
                const stencil = node.add(0x34).readS32();

                const key = node.toString();
                if (seen.has(key)) return;
                seen.add(key);
                if (seen.size > MAX_UNIQUE) return;

                const record = {
                    kind: 'hit',
                    node: key,
                    stencil: stencil,
                    unique_index: seen.size,
                };
                if (seen.size <= DUMP_FIRST_N) {
                    record.dump_0x30_0x50 = dumpNode(node);
                }
                send(record);
            } catch (e) {
                send({ kind: 'error', message: e.message });
            }
        },
    });
    send({ kind: 'installed', hook_offset: HOOK_OFFSET });
}

rpc.exports = {
    setup(offset, maxUnique, dumpFirstN) {
        HOOK_OFFSET = offset >>> 0;     // coerce to uint
        MAX_UNIQUE = maxUnique;
        DUMP_FIRST_N = dumpFirstN;
        install();
        return { base: base.toString(), hook_offset: HOOK_OFFSET };
    },
    stats() {
        return { unique_nodes_seen: seen.size };
    },
};
"""


def _on_message(msg, data):
    if msg.get("type") == "error":
        print(f"[agent-error] {msg.get('stack') or msg}", file=sys.stderr)
        return
    payload = msg.get("payload") or {}
    kind = payload.get("kind")
    if kind == "base":
        print(f"[agent] libkrkr2.so base = {payload['base']}")
    elif kind == "installed":
        print(f"[agent] hook installed at libkrkr2 + 0x{payload['hook_offset']:x}")
    elif kind == "hit":
        idx = payload["unique_index"]
        node = payload["node"]
        stencil = payload["stencil"]
        dump = payload.get("dump_0x30_0x50")
        line = f"[#{idx:03d}] node={node} stencilType={stencil}"
        if dump is not None:
            line += f"  node+0x30..0x50={dump}"
        print(line)
    elif kind == "error":
        print(f"[agent-err] {payload.get('message')}", file=sys.stderr)
    else:
        print(f"[agent?] {payload}", file=sys.stderr)


def _attach_by_name(device, candidates):
    last_err = None
    for name in candidates:
        try:
            return device.attach(name), name
        except Exception as exc:    # frida.ProcessNotFoundError etc.
            last_err = exc
            continue
    raise RuntimeError(
        f"could not attach to any of {candidates!r}: {last_err!r}"
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--process", action="append", default=None,
        help="target process name (repeatable). "
             f"Default tries: {', '.join(DEFAULT_PROCESS_CANDIDATES)}",
    )
    ap.add_argument(
        "--offset", type=lambda s: int(s, 0), default=DEFAULT_OFFSET,
        help=f"libkrkr2.so offset of hook site (default 0x{DEFAULT_OFFSET:x})",
    )
    ap.add_argument(
        "--max-unique", type=int, default=200,
        help="stop logging after this many unique MotionNode pointers",
    )
    ap.add_argument(
        "--dump-first-n", type=int, default=8,
        help="hex-dump node+0x30..0x50 for the first N unique nodes",
    )
    ap.add_argument(
        "--device", default=None,
        help="frida device id (default: first USB/Android device)",
    )
    ap.add_argument(
        "--duration", type=float, default=0.0,
        help="run for N seconds then exit (0 = until Ctrl-C)",
    )
    args = ap.parse_args()

    candidates = tuple(args.process) if args.process else DEFAULT_PROCESS_CANDIDATES

    if args.device:
        device = frida.get_device(args.device, timeout=10.0)
    else:
        device = frida.get_usb_device(timeout=10.0)
    print(f"[host] using device: {device.id} ({device.name})")

    session, proc_name = _attach_by_name(device, candidates)
    print(f"[host] attached to {proc_name!r}")

    script = session.create_script(AGENT_JS)
    script.on("message", _on_message)
    script.load()
    info = script.exports_sync.setup(
        args.offset, args.max_unique, args.dump_first_n,
    )
    print(f"[host] agent ready: {info}")

    try:
        if args.duration > 0:
            time.sleep(args.duration)
        else:
            print("[host] streaming; Ctrl-C to stop.")
            while True:
                time.sleep(1.0)
    except KeyboardInterrupt:
        print("\n[host] interrupted")
    finally:
        try:
            stats = script.exports_sync.stats()
            print(f"[host] final stats: {stats}")
        except Exception:
            pass
        try:
            script.unload()
        except Exception:
            pass
        try:
            session.detach()
        except Exception:
            pass


if __name__ == "__main__":
    main()
