#!/usr/bin/env python3
import argparse
import hashlib
import http.server
import mimetypes
import ssl
import os
import sys
import threading

mimetypes.add_type('application/manifest+json', '.webmanifest')

parser = argparse.ArgumentParser(description='Serve KrKr2 Web build with COOP/COEP headers')
parser.add_argument('serve_dir', nargs='?', default='.', help='Directory to serve (default: .)')
parser.add_argument('http_port', nargs='?', type=int, default=8080, help='HTTP port (default: 8080)')
parser.add_argument('https_port', nargs='?', type=int, default=8443, help='HTTPS port (default: 8443)')
parser.add_argument('--xp3', metavar='FILE', help='Path to a local .xp3 file to serve at /data.xp3')
parser.add_argument('--zip', metavar='FILE', help='Path to a local .zip file to serve at /game.zip')
parser.add_argument('--entry', metavar='NAME', help='Auto-select this .xp3 when zip contains multiple (e.g. data.xp3)')
args = parser.parse_args()

xp3_real_path = os.path.abspath(args.xp3) if args.xp3 else None
if xp3_real_path and not os.path.isfile(xp3_real_path):
    print(f"Error: xp3 file not found: {xp3_real_path}", file=sys.stderr)
    sys.exit(1)

zip_real_path = os.path.abspath(args.zip) if args.zip else None
if zip_real_path and not os.path.isfile(zip_real_path):
    print(f"Error: zip file not found: {zip_real_path}", file=sys.stderr)
    sys.exit(1)

_content_sha256_cache = {}
_content_sha256_lock = threading.Lock()


def content_sha256(real_path):
    """Return a SHA-256 cached by stable file identity and change metadata."""
    stat = os.stat(real_path)
    cache_key = (real_path, stat.st_dev, stat.st_ino, stat.st_size,
                 stat.st_mtime_ns, stat.st_ctime_ns)
    with _content_sha256_lock:
        digest = _content_sha256_cache.get(cache_key)
        if digest is not None:
            return digest
        hasher = hashlib.sha256()
        with open(real_path, 'rb') as source:
            while True:
                chunk = source.read(8 * 1024 * 1024)
                if not chunk:
                    break
                hasher.update(chunk)
        digest = hasher.hexdigest()
        _content_sha256_cache.clear()
        _content_sha256_cache[cache_key] = digest
        return digest


def if_none_match_matches(header_value, etag):
    """Use weak comparison for If-None-Match as required for GET and HEAD."""
    if not header_value:
        return False
    for candidate in header_value.split(','):
        candidate = candidate.strip()
        if candidate == '*':
            return True
        if candidate.startswith('W/'):
            candidate = candidate[2:].lstrip()
        if candidate == etag:
            return True
    return False


class COIHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, HEAD, OPTIONS')
        self.send_header('Access-Control-Allow-Headers',
                         'If-None-Match, Range')
        self.send_header('Access-Control-Expose-Headers',
                         'Accept-Ranges, Content-Length, Content-Range, '
                         'ETag')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if xp3_real_path and self.path == '/data.xp3':
            self._serve_file(xp3_real_path)
        elif zip_real_path and self.path == '/game.zip':
            self._serve_file(zip_real_path)
        else:
            super().do_GET()

    def do_HEAD(self):
        if xp3_real_path and self.path == '/data.xp3':
            self._serve_file(xp3_real_path, head_only=True)
        elif zip_real_path and self.path == '/game.zip':
            self._serve_file(zip_real_path, head_only=True)
        else:
            super().do_HEAD()

    def _serve_file(self, real_path, head_only=False):
        # 支持 HTTP Range：VLFS 的 ?xp3= 远程懒加载按 1MiB 分块拉取
        try:
            size = os.path.getsize(real_path)
            sha256 = content_sha256(real_path)
            etag = f'"sha256-{sha256}"'

            if if_none_match_matches(self.headers.get('If-None-Match'), etag):
                self.send_response(304)
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('ETag', etag)
                self.end_headers()
                return

            start, end = 0, size - 1
            range_header = self.headers.get('Range')
            # If-Range only permits a strong ETag match. Dates and weak tags are
            # deliberately treated as mismatches because this server emits an
            # entity tag validator for every served archive.
            if_range = self.headers.get('If-Range')
            if range_header and if_range and if_range.strip() != etag:
                range_header = None
            is_partial = False
            if range_header and range_header.startswith('bytes='):
                spec = range_header[len('bytes='):].split(',')[0].strip()
                lo, _, hi = spec.partition('-')
                if lo:
                    start = int(lo)
                    end = int(hi) if hi else size - 1
                elif hi:  # 后缀范围 bytes=-N
                    start = max(0, size - int(hi))
                if start >= size:
                    self.send_response(416)
                    self.send_header('Content-Range', f'bytes */{size}')
                    self.end_headers()
                    return
                end = min(end, size - 1)
                is_partial = True

            self.send_response(206 if is_partial else 200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Content-Length', str(end - start + 1))
            self.send_header('ETag', etag)
            if is_partial:
                self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
            self.end_headers()
            if not head_only:
                with open(real_path, 'rb') as f:
                    f.seek(start)
                    remaining = end - start + 1
                    while remaining > 0:
                        chunk = f.read(min(1024 * 1024, remaining))
                        if not chunk:
                            break
                        remaining -= len(chunk)
                        self.wfile.write(chunk)
        except BrokenPipeError:
            pass
        except Exception as e:
            self.send_error(500, str(e))

    def log_message(self, fmt, *a):
        sys.stderr.write(f"  {self.address_string()} - {fmt % a}\n")

os.chdir(args.serve_dir)
script_dir = os.path.dirname(os.path.abspath(__file__))
certfile = os.path.join(script_dir, 'server.crt')
keyfile = os.path.join(script_dir, 'server.key')

if xp3_real_path:
    url_param = '?xp3=/data.xp3'
elif zip_real_path:
    url_param = '?game=/game.zip'
    if args.entry:
        url_param += '&entry=' + args.entry
else:
    url_param = ''

# ThreadingHTTPServer 必需：WebKit/Safari 会 preconnect（先开 TCP 不发请求），
# 单线程 HTTPServer 会被空连接阻塞，所有后续资源请求排队导致页面永远加载不完
http_server = http.server.ThreadingHTTPServer(('0.0.0.0', args.http_port), COIHandler)
print(f"  HTTP  -> http://localhost:{args.http_port}/index.html{url_param}  (localhost debug)")

if os.path.exists(certfile) and os.path.exists(keyfile):
    https_server = http.server.ThreadingHTTPServer(('0.0.0.0', args.https_port), COIHandler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile, keyfile)
    https_server.socket = ctx.wrap_socket(https_server.socket, server_side=True)
    print(f"  HTTPS -> https://<your-ip>:{args.https_port}/index.html{url_param}  (LAN access)")
    threading.Thread(target=https_server.serve_forever, daemon=True).start()
else:
    print(f"  HTTPS not enabled (no server.crt / server.key found)")
    print(f"  Generate cert: openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes")

if xp3_real_path:
    print(f"  XP3   -> /data.xp3  ({xp3_real_path})")
if zip_real_path:
    print(f"  ZIP   -> /game.zip  ({zip_real_path})")

http_server.serve_forever()
