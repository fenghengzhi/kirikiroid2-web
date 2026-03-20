#!/usr/bin/env python3
import argparse
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

class COIHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

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
        try:
            size = os.path.getsize(real_path)
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(size))
            self.end_headers()
            if not head_only:
                with open(real_path, 'rb') as f:
                    while True:
                        chunk = f.read(1024 * 1024)
                        if not chunk:
                            break
                        self.wfile.write(chunk)
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

http_server = http.server.HTTPServer(('0.0.0.0', args.http_port), COIHandler)
print(f"  HTTP  -> http://localhost:{args.http_port}/index.html{url_param}  (localhost debug)")

if os.path.exists(certfile) and os.path.exists(keyfile):
    https_server = http.server.HTTPServer(('0.0.0.0', args.https_port), COIHandler)
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
