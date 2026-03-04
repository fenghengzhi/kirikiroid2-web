#!/usr/bin/env python3
import http.server
import ssl
import os
import sys

class COIHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

os.chdir(sys.argv[1] if len(sys.argv) > 1 else '.')
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
script_dir = os.path.dirname(os.path.abspath(__file__))
certfile = os.path.join(script_dir, 'server.crt')
keyfile = os.path.join(script_dir, 'server.key')

server = http.server.HTTPServer(('0.0.0.0', port), COIHandler)

if os.path.exists(certfile) and os.path.exists(keyfile):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile, keyfile)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print(f"Serving on https://localhost:{port} with COOP/COEP headers (HTTPS)")
else:
    print(f"Serving on http://localhost:{port} with COOP/COEP headers (HTTP)")
    print("  Note: SharedArrayBuffer requires HTTPS for non-localhost access.")
    print(f"  Generate cert: openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes")

server.serve_forever()
