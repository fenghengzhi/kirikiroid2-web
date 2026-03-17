#!/usr/bin/env python3
import argparse
import http.server
import ssl
import os
import sys
import threading

parser = argparse.ArgumentParser(description='Serve KiriKiroid2 Web build with COOP/COEP headers')
parser.add_argument('serve_dir', nargs='?', default='.', help='Directory to serve (default: .)')
parser.add_argument('http_port', nargs='?', type=int, default=8080, help='HTTP port (default: 8080)')
parser.add_argument('https_port', nargs='?', type=int, default=8443, help='HTTPS port (default: 8443)')
parser.add_argument('--xp3', metavar='FILE', help='Path to a local .xp3 file to serve at /data.xp3')
args = parser.parse_args()

xp3_real_path = os.path.abspath(args.xp3) if args.xp3 else None
if xp3_real_path and not os.path.isfile(xp3_real_path):
    print(f"Error: xp3 file not found: {xp3_real_path}", file=sys.stderr)
    sys.exit(1)

class COIHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

    def do_GET(self):
        if xp3_real_path and self.path == '/data.xp3':
            self._serve_xp3()
        else:
            super().do_GET()

    def do_HEAD(self):
        if xp3_real_path and self.path == '/data.xp3':
            self._serve_xp3(head_only=True)
        else:
            super().do_HEAD()

    def _serve_xp3(self, head_only=False):
        try:
            size = os.path.getsize(xp3_real_path)
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(size))
            self.end_headers()
            if not head_only:
                with open(xp3_real_path, 'rb') as f:
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

xp3_param = '?xp3=/data.xp3' if xp3_real_path else ''

http_server = http.server.HTTPServer(('0.0.0.0', args.http_port), COIHandler)
print(f"  HTTP  -> http://localhost:{args.http_port}/index.html{xp3_param}  (localhost debug)")

if os.path.exists(certfile) and os.path.exists(keyfile):
    https_server = http.server.HTTPServer(('0.0.0.0', args.https_port), COIHandler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile, keyfile)
    https_server.socket = ctx.wrap_socket(https_server.socket, server_side=True)
    print(f"  HTTPS -> https://<your-ip>:{args.https_port}/index.html{xp3_param}  (LAN access)")
    threading.Thread(target=https_server.serve_forever, daemon=True).start()
else:
    print(f"  HTTPS not enabled (no server.crt / server.key found)")
    print(f"  Generate cert: openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes")

if xp3_real_path:
    print(f"  XP3   -> /data.xp3  ({xp3_real_path})")

http_server.serve_forever()
