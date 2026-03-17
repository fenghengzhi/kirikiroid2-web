# KrKr2 Web

A WebAssembly port of the **KiriKiri2 engine** (T Visual Presenter), allowing KiriKiri engine games to run directly in modern browsers.

> This is a personal fork focused exclusively on the Web platform. Pull requests are not accepted.

**语言 / Language**: [中文](README_CN.md) | English

---

## Supported Browsers

Chrome, Edge, Firefox, Safari (any browser with WebAssembly + SharedArrayBuffer support).

---

## Build

### Prerequisites

- [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html)
- [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
- [ninja](https://github.com/ninja-build/ninja/releases)
- [cmake 3.31.1+](https://cmake.org/download/)
- `bison 3.8.2+`
- `python3`

### Environment Variables

```bash
export VCPKG_ROOT=/path/to/vcpkg
source /path/to/emsdk/emsdk_env.sh   # sets EMSDK automatically
```

### Build Steps

> **Note**: Only Release builds are supported. Debug builds will crash with a stack overflow due to Asyncify instrumentation on the TJS compiler's recursive descent parser.

```bash
cmake --preset "Web Release Config"
cmake --build out/web/release
```

### Output Files

```
out/web/release/
  index.html
  index.js
  index.wasm
  index.data
  index.worker.js
```

---

## Running

The Web build requires [Cross-Origin Isolation](https://web.dev/cross-origin-isolation-guide/) headers (`COOP` / `COEP`) for `SharedArrayBuffer`. A regular HTTP server will not work.

Use the included `coi-server.py`:

```bash
python3 coi-server.py out/web/release [http_port] [https_port] [--xp3 game.xp3]
```

The server starts:
- **HTTP** on port 8080 (default) — for `localhost` debugging
- **HTTPS** on port 8443 (default) — for LAN access from other devices

Then open `http://localhost:8080/index.html` in your browser.

### Serving a Game File Directly

Use `--xp3` to have the server host a local `.xp3` file:

```bash
python3 coi-server.py out/web/release --xp3 /path/to/game/data.xp3
```

The file is served at `/data.xp3`, and the printed URL includes the `?xp3=` query parameter. Opening that URL will automatically download and start the game without manual file selection.

You can also specify any accessible `.xp3` URL manually:

```
http://localhost:8080/index.html?xp3=/data.xp3
```

### HTTPS for LAN Access

`SharedArrayBuffer` requires `localhost` or HTTPS. To enable HTTPS for LAN access, place `server.crt` and `server.key` alongside `coi-server.py`:

```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
```

---

## TODO

- [ ] Switch from Asyncify + `NO_DISABLE_EXCEPTION_CATCHING` to JSPI + `-fwasm-exceptions` once iOS Safari supports [JSPI (JavaScript Promise Integration)](https://github.com/aspect-build/aspect-cli/issues/1). This will eliminate `invoke_*`-based exception handling, significantly reduce wasm binary size (~26MB → ~17MB), and lower per-Worker memory overhead.

---

## Supported Games

See [games list](./doc/support_games.txt).

---

## License

MIT License. See [LICENSE](./LICENSE) for details.
