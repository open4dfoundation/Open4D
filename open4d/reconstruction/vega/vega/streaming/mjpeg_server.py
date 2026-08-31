"""Minimal MJPEG-over-HTTP server: pushes whatever JPEG frame was last
`update()`-d to any number of connected browsers via
`multipart/x-mixed-replace`. This is the "last hop" of the live demo — all
decoding/rendering happens elsewhere (see `vega.pipeline`); this module only
ever touches already-encoded JPEG bytes.
"""
from __future__ import annotations

import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

BOUNDARY = "vegaframe"


class FrameBuffer:
    def __init__(self):
        self._cond = threading.Condition()
        self._jpeg: bytes | None = None
        self._seq = 0

    def update(self, jpeg_bytes: bytes):
        with self._cond:
            self._jpeg = jpeg_bytes
            self._seq += 1
            self._cond.notify_all()

    def wait_for_next(self, last_seq: int, timeout: float = 10.0):
        with self._cond:
            ok = self._cond.wait_for(lambda: self._seq != last_seq, timeout=timeout)
            if not ok:
                return None, last_seq
            return self._jpeg, self._seq


def make_handler(frame_buffer: FrameBuffer, status_html_fn=None):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass  # keep console quiet

        def do_GET(self):
            # Match on the path alone. Callers append a cache-busting query
            # (`/stream?t=...`) to stop a browser reusing a previous run's
            # stream, and comparing the raw `self.path` would 404 on it —
            # which shows up as a page that loads but stays blank.
            route = urlsplit(self.path).path
            if route in ("/", "/index.html") and status_html_fn is not None:
                body = status_html_fn().encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                # The page embeds the stream, so a cached copy of it can pin a
                # browser to a previous run's scene.
                self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
                self.send_header("Pragma", "no-cache")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            if route in ("/", "/stream"):
                self.send_response(200)
                self.send_header("Age", "0")
                self.send_header("Cache-Control", "no-store, no-cache, private")
                self.send_header("Pragma", "no-cache")
                self.send_header("Content-Type", f"multipart/x-mixed-replace; boundary={BOUNDARY}")
                self.end_headers()
                last_seq = 0
                try:
                    while True:
                        jpeg, last_seq = frame_buffer.wait_for_next(last_seq)
                        if jpeg is None:
                            continue
                        self.wfile.write(f"--{BOUNDARY}\r\n".encode())
                        self.send_header("Content-Type", "image/jpeg")
                        self.send_header("Content-Length", str(len(jpeg)))
                        self.end_headers()
                        self.wfile.write(jpeg)
                        self.wfile.write(b"\r\n")
                except (BrokenPipeError, ConnectionResetError):
                    pass
                return

            self.send_response(404)
            self.end_headers()

    return Handler


def serve_forever(frame_buffer: FrameBuffer, port: int, status_html_fn=None) -> ThreadingHTTPServer:
    handler = make_handler(frame_buffer, status_html_fn)
    httpd = ThreadingHTTPServer(("0.0.0.0", port), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd
