#!/usr/bin/env python3
import http.server
import os
import sys

class COIHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

os.chdir(sys.argv[1] if len(sys.argv) > 1 else '.')
server = http.server.HTTPServer(('0.0.0.0', 8080), COIHandler)
print(f"Serving on http://localhost:8080 with COOP/COEP headers")
server.serve_forever()
