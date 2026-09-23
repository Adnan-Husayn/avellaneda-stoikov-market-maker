#!/usr/bin/env python3
"""Zero-dependency static file server for the web dashboard.

Run the simulation first so it starts writing state.json/history.json:
    ./build/mm_web_runner &

Then serve the dashboard:
    python3 gui_web/server.py

and open http://localhost:8000

Also accepts POST /control, which the dashboard uses to send new model
parameters. The body is written as-is to gui_web/control.json, which
mm_web_runner polls and applies (restarting the simulation) when it sees
a new "seq" value.
"""

import http.server
import json
import os
import socketserver

PORT = 8000
GUI_WEB_DIR = os.path.dirname(__file__)
STATIC_DIR = os.path.join(GUI_WEB_DIR, "static")
CONTROL_PATH = os.path.join(GUI_WEB_DIR, "control.json")


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=STATIC_DIR, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_POST(self):
        if self.path != "/control":
            self.send_error(404, "Not found")
            return

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        try:
            payload = json.loads(body)
            if "seq" not in payload:
                raise ValueError("missing 'seq'")
        except (json.JSONDecodeError, ValueError) as e:
            self.send_error(400, f"Bad control payload: {e}")
            return

        tmp_path = CONTROL_PATH + ".tmp"
        with open(tmp_path, "w") as f:
            # Compact, no spaces — the C++ side's JSON reader expects "key":value.
            json.dump(payload, f, separators=(",", ":"))
        os.replace(tmp_path, CONTROL_PATH)

        self.send_response(204)
        self.end_headers()


def main():
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        print(f"Serving {STATIC_DIR} at http://localhost:{PORT}")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
