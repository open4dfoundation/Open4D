"""Push JPEG frames to a browser over ``multipart/x-mixed-replace``.

The last hop, and the only one available to this method: a neural field cannot
be decoded in a browser -- there is no geometry to send and the entropy coder
is a Python 3.8 binary -- so ReRF is decoded and rendered where the GPU is and
what reaches the viewer is pixels.

MJPEG rather than a video codec because it needs no negotiation, no JavaScript
and no build step: ``multipart/x-mixed-replace`` is a browser feature, and an
``<img>`` pointed at this endpoint plays. It is not efficient -- every frame is
an independent JPEG, with no inter-frame compression at all -- and that is a
real cost worth stating, not hiding. It buys a stream that works through one
forwarded port with nothing installed.

Local rather than shared with the other methods in this repository. The obvious
alternative is for every method to push into one server, and that was the
previous arrangement -- but `streamer` requires Python 3.10 and this package
requires 3.8, so a shared module would have to be dependency-free and pinned to
the older interpreter to satisfy a method that may not even need it. Sixty
lines here costs less than that coupling.
"""
from __future__ import annotations

import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

BOUNDARY = "rerfframe"


class FrameBuffer:
    """The most recent frame, and a way to wait for the next one.

    One slot, not a queue: a viewer that falls behind should see the current
    frame next, not work through a backlog of stale ones. A renderer that
    outpaces its viewers simply overwrites, which is the correct behaviour for
    live pixels and the reason there is no buffering policy here.
    """

    def __init__(self) -> None:
        self._changed = threading.Condition()
        self._jpeg = None
        self._sequence = 0

    def update(self, jpeg: bytes) -> None:
        with self._changed:
            self._jpeg = jpeg
            self._sequence += 1
            self._changed.notify_all()

    def wait_for_next(self, last: int, timeout: float = 10.0):
        """The next frame after sequence ``last``, or ``(None, last)`` on timeout."""
        with self._changed:
            arrived = self._changed.wait_for(
                lambda: self._sequence != last, timeout=timeout
            )
            if not arrived:
                return None, last
            return self._jpeg, self._sequence


def make_handler(frames: FrameBuffer, status_html=None):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):
            pass                        # one line per frame would be unreadable

        def do_GET(self):               # noqa: N802 - the base class names it
            # The query string is ignored when matching, so a cache-busting
            # /stream?t=... still routes: a browser holding an open /stream from
            # an earlier run would otherwise keep showing that run's clip.
            route = urlsplit(self.path).path
            if route == "/stream":
                return self._stream()
            if route in ("/", "/index.html"):
                return self._status()
            self.send_error(404)

        def _status(self):
            body = (status_html() if status_html else "<html><body/></html>").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _stream(self):
            self.send_response(200)
            self.send_header(
                "Content-Type", f"multipart/x-mixed-replace; boundary={BOUNDARY}"
            )
            self.send_header("Cache-Control", "no-store, no-cache, private")
            # No Content-Length: the body never ends, so it is delimited by
            # close. Anything that waits for completion waits forever.
            self.send_header("Connection", "close")
            self.close_connection = True
            self.end_headers()
            last = 0
            try:
                while True:
                    jpeg, last = frames.wait_for_next(last)
                    if jpeg is None:
                        continue        # renderer is slow or stalled; keep waiting
                    self.wfile.write(
                        f"--{BOUNDARY}\r\nContent-Type: image/jpeg\r\n"
                        f"Content-Length: {len(jpeg)}\r\n\r\n".encode()
                    )
                    self.wfile.write(jpeg)
                    self.wfile.write(b"\r\n")
            except (BrokenPipeError, ConnectionResetError):
                pass                    # tab closed or navigated away; expected

    return Handler


def serve(frames: FrameBuffer, port: int, status_html=None) -> ThreadingHTTPServer:
    """Serve ``frames`` on ``port``, on a daemon thread. Returns the server."""
    server = ThreadingHTTPServer(("0.0.0.0", port), make_handler(frames, status_html))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server
