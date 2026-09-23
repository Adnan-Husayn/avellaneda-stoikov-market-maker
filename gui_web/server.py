#!/usr/bin/env python3
"""Zero-dependency static file server for the web dashboard.

Run the simulation first so it starts writing state.json/history.json:
    ./build/mm_web_runner &

Then serve the dashboard:
    python3 gui_web/server.py

and open http://localhost:8000
"""

import http.server
import os
import socketserver

PORT = 8000
STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=STATIC_DIR, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        print(f"Serving {STATIC_DIR} at http://localhost:{PORT}")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
