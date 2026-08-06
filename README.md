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
  index.js
  index.wasm
  index.worker.js
  build-config.js
  assets.zip
```

This is the **engine only** — there is no HTML page here. The web frontend is a
separate Vue 3 + Vite project under `platforms/web/webui/`, built independently:

```bash
cd platforms/web/webui
npm install
npm run build          # -> platforms/web/webui/dist
```

Vite pulls the engine files above out of `out/web/release` (override with
`KRKR2_ENGINE_DIR`) and emits them into `dist` alongside the page, the PWA
assets and a generated `sw.js`. Because the two builds are independent,
**changing the frontend never relinks the wasm** — and it does not require emsdk
at all. See `platforms/web/webui/README.md` for the frontend and its Cloudflare
Worker backend.

---

## Running

The Web build requires [Cross-Origin Isolation](https://web.dev/cross-origin-isolation-guide/) headers (`COOP` / `COEP`) for `SharedArrayBuffer`. A regular HTTP server will not work.

Serve the Vite output (`platforms/web/webui/dist`) with the included
`coi-server.py`:

```bash
python3 coi-server.py platforms/web/webui/dist [http_port] [https_port] [options]
```

The server starts:
- **HTTP** on port 8080 (default) — for `localhost` debugging
- **HTTPS** on port 8443 (default) — for LAN access from other devices

Then open `http://localhost:8080/` in your browser (the game library). The player
page is `/play.html`; `--xp3` / `--zip` below print the exact URL to use.

### Serving a Game File Directly

#### Single .xp3 file

Use `--xp3` to have the server host a local `.xp3` file:

```bash
python3 coi-server.py platforms/web/webui/dist --xp3 /path/to/game/data.xp3
```

The file is served at `/data.xp3`, and the printed URL includes the `?xp3=` query parameter. Opening that URL will automatically download and start the game without manual file selection.

#### ZIP archive

Use `--zip` to serve a `.zip` archive containing the game files. The web page will extract it in-browser and load the game:

```bash
python3 coi-server.py platforms/web/webui/dist --zip /path/to/game.zip
```

If the archive contains multiple `.xp3` files, a selection dialog will appear. Use `--entry` to auto-select one:

```bash
python3 coi-server.py platforms/web/webui/dist --zip /path/to/game.zip --entry data.xp3
```

#### URL parameters

You can also pass game sources via URL query parameters directly:

| Parameter | Description |
|-----------|-------------|
| `?xp3=<url>` | Load a single `.xp3` file from the given URL |
| `?game=<url>` | Download and extract a `.zip` archive from the given URL |
| `?entry=<name>` | Auto-select this `.xp3` when the archive contains multiple (use with `game`) |

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
