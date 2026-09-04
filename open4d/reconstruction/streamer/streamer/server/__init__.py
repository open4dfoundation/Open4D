"""Serving a bundle to the playback client.

Deliberately dumb: static files out of the bundle directory, with ``/`` rewritten
to the packaged client (`streamer.client`) so the bundle itself never has to
carry a copy of the page. It binds loopback unless told otherwise, since a
bundle is research output on a shared machine and this server has no
authentication of any kind.

This is the transport a *local* bundle needs, and it is the simplest of the
transports the streaming model admits: every frame is a file, reachable in one
request, in any order. See `open4d.core.Dependency` for the ones that are not --
a codec whose frames depend on a key frame, or whose decode stream cannot be
rewound, needs a scheduler over this rather than a different server.
"""

from __future__ import annotations

import errno
import http.server
import json
import socket
import socketserver
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from functools import partial
from pathlib import Path

from .. import bundle, live, representations
from ..link import Link, described
from ..client import viewer_path
from ..monitor import Monitor

DEFAULT_PORT = 8770


VIEWER_ROUTES = ("/", "/index.html", "/viewer.html")
STATS_ROUTE = "/stats.json"
#: Live streams are proxied under this prefix; see `streamer.live`.
LIVE_PREFIX = f"/{live.ROUTE_PREFIX}/"
#: Assets that belong to the client package rather than to any bundle -- the
#: vendored Draco decoder, for one. Served from here so the page fetches them
#: from its own origin and never a CDN, and so a bundle does not have to carry
#: a copy of a decoder it did not choose.
CLIENT_PREFIX = "/client/"
#: Copy size for the proxy. Small, because a frame boundary can fall anywhere
#: and a large buffer would hold the tail of one frame back until the next.
PROXY_CHUNK = 8192
#: Copy size for a range response. Larger than the proxy's: there is no frame
#: boundary to respect, and a 100 MB clip should not be a million writes.
RANGE_CHUNK = 1 << 16

#: Content types for client-package assets. `.wasm` matters: a browser refuses
#: to compile a module served as anything else through the streaming API.
CLIENT_TYPES = {
    ".js": "text/javascript",
    ".wasm": "application/wasm",
    ".html": "text/html; charset=utf-8",
    ".md": "text/plain; charset=utf-8",
}


def _parse_range(header: str, size: int):
    """``(start, end)`` inclusive for a single byte range, or None if unusable.

    Handles the three forms that occur: ``bytes=0-99`` explicit,
    ``bytes=500-`` open ended, and ``bytes=-500`` meaning the last 500. None
    means answer 416, which is what a start past the end of the file deserves
    -- returning the whole file there would let a resuming client silently
    append a second copy to what it already had.
    """
    if not header.startswith("bytes=") or "," in header:
        return None
    spec = header[len("bytes="):].strip()
    first, _, last = spec.partition("-")
    try:
        if not first:                       # bytes=-N, the final N bytes
            if not last:
                return None
            length = int(last)
            if length <= 0:
                return None
            return max(0, size - length), size - 1
        start = int(first)
        end = int(last) if last else size - 1
    except ValueError:
        return None
    if start < 0 or start >= size or end < start:
        return None
    return start, min(end, size - 1)


class _ShapedWriter:
    """Wraps a socket's write file so every byte is paced by a `Link`.

    Wrapping the file rather than pacing each route is what makes the shaping
    total: headers, bodies, range responses and the live proxy all leave
    through here, and a route added later is shaped without knowing a link
    exists. Pacing at the routes instead would have left whichever one was
    written next unshaped, and an unshaped route in a measurement is not a
    smaller effect -- it is the whole result, since that is where the bytes go.

    The propagation delay is charged on the first write of each response, not
    per chunk: a stream of bytes already in flight pays the flight time once.
    """

    def __init__(self, wfile, link: Link) -> None:
        self._wfile = wfile
        self._link = link
        self._fresh = True

    def restart(self) -> None:
        """Called per request, so each response pays propagation once."""
        self._fresh = True

    def write(self, data):
        if data:
            self._link.send(len(data), propagate=self._fresh)
            self._fresh = False
        return self._wfile.write(data)

    def __getattr__(self, name):
        return getattr(self._wfile, name)


class _Handler(http.server.SimpleHTTPRequestHandler):
    """Static files from the bundle, plus the client page and the counters.

    ``monitor`` is set per-server by :func:`serve`; a handler class with no
    monitor still works, which is what keeps this usable as a plain static
    server.

    **HTTP/1.1, so a connection is reused across frames.** The base class
    defaults to 1.0, which closes after every response -- one TCP handshake and
    one fresh slow-start per frame. On loopback that is invisible, which is why
    it survived; over a link with 20 ms of round trip a 59 kB Draco frame then
    costs a setup RTT plus roughly three more while the congestion window
    opens, so about 80 ms a frame and a ~12 fps ceiling *regardless of
    bandwidth*. Any rate this server appears to sustain would be measuring that
    rather than the network, which makes it the first thing to fix before
    measuring anything.

    1.1 requires every response to be self-delimiting, or a client waits for a
    body that never ends. Two consequences, both handled below: every response
    here carries a ``Content-Length``, and the one that cannot -- the live
    proxy, whose body is endless -- says ``Connection: close`` and means it.

    It also means an idle client holds a thread until it goes away, so
    ``timeout`` reaps connections that stop talking.
    """

    protocol_version = "HTTP/1.1"

    #: Turn off Nagle's algorithm. Not an optimisation -- without it keep-alive
    #: is *slower* than the connection-per-request it replaces, and measurably:
    #: 30 frames took 1.20 s against 0.01 s, 40 ms each, on loopback.
    #:
    #: The cause is Nagle meeting delayed ACK. A response leaves here as two
    #: writes, headers then body, because the base class buffers the headers
    #: and flushes them at ``end_headers``. Nagle holds the second small write
    #: until the first is acknowledged; the client's delayed-ACK timer sits on
    #: that acknowledgement for ~40 ms. Closing the connection used to mask it,
    #: since the FIN pushes everything out at once -- so this only appeared
    #: once keep-alive worked.
    #:
    #: 40 ms a frame is a 25 fps ceiling on loopback, which would have made the
    #: transport worse than before while looking like progress.
    disable_nagle_algorithm = True

    #: Seconds an idle keep-alive connection is held before it is dropped.
    #: Without this a browser tab left open pins a thread indefinitely, and a
    #: threaded server with unbounded idle connections eventually stops
    #: accepting.
    timeout = 30

    monitor: Monitor | None = None
    #: A shared bottleneck, or None for an unshaped loopback server. Shared on
    #: purpose: concurrent panes have to contend for one pipe, or the budget a
    #: chooser is given means nothing.
    link: Link | None = None
    #: Clip name -> upstream URL, from the manifest. Empty for a bundle with no
    #: live clips, which makes the proxy route 404 rather than exist unused.
    upstreams: dict = {}

    # SimpleHTTPRequestHandler guesses by extension and falls back to
    # text/html, which makes a .ply arrive as markup and fail to parse. The
    # suffixes come from the representation registry rather than a list here, so
    # registering a representation is all it takes to serve its frames.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        **representations.media_types(),
        ".json": "application/json",
    }

    def do_GET(self):  # noqa: N802 - the base class names it
        if self.path in VIEWER_ROUTES:
            return self._send_viewer()
        if self.path.split("?", 1)[0] == STATS_ROUTE:
            return self._send_stats()
        if self.path.startswith(LIVE_PREFIX):
            return self._proxy_live(self.path[len(LIVE_PREFIX):])
        if self.path.startswith(CLIENT_PREFIX):
            return self._send_client_asset(self.path[len(CLIENT_PREFIX):])
        self._offer_ranges = True
        if self.headers.get("Range"):
            return self._send_range()
        return super().do_GET()

    def do_HEAD(self):  # noqa: N802
        if self.path in VIEWER_ROUTES:
            return self._send_viewer(body=False)
        self._offer_ranges = True
        return super().do_HEAD()

    def end_headers(self):
        # Advertised here because the base class ends its own headers, leaving
        # no later point to add one. Only on the static-file path, since that
        # is the only route that honours a Range.
        if getattr(self, "_offer_ranges", False):
            self.send_header("Accept-Ranges", "bytes")
            self._offer_ranges = False
        super().end_headers()

    def _send_range(self):
        """Serve a byte range of a bundle file.

        What this buys is resumption: `streamer.transfer` fetching a 100 MB
        Gaussian clip over a tunnel that drops can continue from where it
        stopped instead of starting the file again. Without it the only
        recovery is re-downloading, which on a big clip is the difference
        between seconds and minutes.

        Only a single range is honoured. Multipart ranges exist in the
        standard, are used by essentially nothing, and would need a different
        body format; asking for several gets the whole file, which is a
        response the standard permits and every client handles.
        """
        path = Path(self.translate_path(self.path))
        if not path.is_file():
            return super().do_GET()          # let the base class 404 or index
        size = path.stat().st_size
        span = _parse_range(self.headers.get("Range", ""), size)
        if span is None:
            self.send_response(416, "Requested Range Not Satisfiable")
            self.send_header("Content-Range", f"bytes */{size}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        start, end = span
        length = end - start + 1
        self.send_response(206, "Partial Content")
        self.send_header("Content-Type", self.guess_type(str(path)))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(length))
        self.end_headers()
        with open(path, "rb") as handle:
            handle.seek(start)
            remaining = length
            while remaining > 0:
                block = handle.read(min(RANGE_CHUNK, remaining))
                if not block:
                    break
                self.wfile.write(block)
                remaining -= len(block)

    def _send_stats(self):
        """What has gone over the wire, as JSON. Absent without a monitor."""
        if self.monitor is None:
            self.send_error(404, "this server keeps no counters")
            return
        snapshot = self.monitor.snapshot()
        if self.link is not None:
            snapshot["link"] = self.link.observed()
        payload = json.dumps(snapshot, indent=2).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def _send_client_asset(self, relative: str):
        """A file from the client package, e.g. the vendored Draco decoder."""
        root = viewer_path().parent
        # Resolved and then checked to be inside the package: the path comes off
        # a URL, so `../../etc/passwd` is a request this will receive eventually.
        target = (root / relative.split("?", 1)[0]).resolve()
        if not target.is_file() or root.resolve() not in target.parents:
            self.send_error(404, f"no client asset {relative!r}")
            return
        payload = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", CLIENT_TYPES.get(
            target.suffix.lower(), "application/octet-stream"))
        self.send_header("Content-Length", str(len(payload)))
        # Immutable: these ship with the package, so a version of the page and a
        # version of its decoder always arrive together.
        self.send_header("Cache-Control", "public, max-age=86400")
        self.end_headers()
        self.wfile.write(payload)

    def _proxy_live(self, name: str):
        """Relay a live stream from its renderer, so the page has one origin.

        The alternative is putting the renderer's own URL in the manifest, which
        asks the *browser* to reach that port -- and a browser on a laptop
        looking at a tunnelled page cannot, so the pane stays blank and nothing
        reports why. Proxying costs a thread and a copy loop and removes the
        whole class of problem.

        Copied through rather than buffered: this is a `multipart/x-mixed-replace`
        body that never ends, so anything that waits for completion waits
        forever.
        """
        upstream = self.upstreams.get(name.split("?", 1)[0])
        if upstream is None:
            self.send_error(404, f"no live stream named {name!r} in this bundle")
            return
        try:
            source = urllib.request.urlopen(upstream, timeout=10)
        except (urllib.error.URLError, OSError) as error:
            # 502 with the reason, because "the renderer is not running" is the
            # single most likely thing to be wrong and is not the bundle's fault.
            self.send_error(502, f"cannot reach {upstream}: {error}")
            return

        self.send_response(200)
        for header in ("Content-Type", "Age", "Cache-Control", "Pragma"):
            value = source.headers.get(header)
            if value:
                self.send_header(header, value)
        self.send_header("Cache-Control", "no-store, no-cache, private")
        # The one response here with no Content-Length: a
        # multipart/x-mixed-replace body never ends. Under HTTP/1.1 that has to
        # be delimited by the close, so say so and hold the connection for this
        # stream alone rather than trying to reuse it afterwards.
        self.send_header("Connection", "close")
        self.close_connection = True
        self.end_headers()
        try:
            while True:
                block = source.read(PROXY_CHUNK)
                if not block:
                    break
                self.wfile.write(block)
        except (BrokenPipeError, ConnectionResetError):
            pass          # the tab was closed or navigated away; expected
        finally:
            source.close()

    def send_response(self, code, message=None):
        # Captured here because this is the one place every response passes
        # through with its status known, including the base class's static-file
        # path and its errors -- wrapping only do_GET would miss both.
        self._status = code
        super().send_response(code, message)

    def send_header(self, keyword, value):
        if keyword.lower() == "content-length":
            self._size = int(value)
        super().send_header(keyword, value)

    def setup(self):
        super().setup()
        if self.link is not None:
            self.wfile = _ShapedWriter(self.wfile, self.link)

    def handle_one_request(self):
        if isinstance(self.wfile, _ShapedWriter):
            self.wfile.restart()
        self._status, self._size, started = 200, 0, time.monotonic()
        super().handle_one_request()
        if self.monitor is not None and self.path:
            self.monitor.record(
                self.path, self._status, self._size, time.monotonic() - started
            )

    def _send_viewer(self, *, body: bool = True):
        try:
            payload = viewer_path().read_bytes()
        except OSError as error:
            self.send_error(500, f"viewer is missing: {error}")
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if body:
            self.wfile.write(payload)

    def log_message(self, fmt, *args):
        # One line per request would bury the URL the user needs; errors still
        # surface through log_error, which the base class routes here with a
        # different format string.
        if not str(args[0] if args else "").startswith(("GET /frame", "GET /", "HEAD")):
            super().log_message(fmt, *args)


class _Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def _reachable_address(host: str, port: int) -> str:
    """The URL to print. For a wildcard bind, the LAN address is the useful one."""
    if host not in ("0.0.0.0", "::", ""):
        return f"http://{host}:{port}/"
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("8.8.8.8", 80))
        return f"http://{probe.getsockname()[0]}:{port}/"
    except OSError:
        return f"http://{socket.gethostname()}:{port}/"
    finally:
        probe.close()


def serve(
    bundle_dir: Path | str,
    *,
    host: str = "127.0.0.1",
    port: int = DEFAULT_PORT,
    open_browser: bool = False,
    block: bool = True,
    monitor: Monitor | None = None,
    link: Link | None = None,
) -> _Server:
    """Serve ``bundle_dir`` with the client at ``/`` and counters at ``/stats.json``.

    Returns the server, with the `Monitor` it is recording into attached as
    ``server.monitor`` -- which is how a caller measures a playback without
    scraping the JSON back out of its own process. Pass ``monitor`` to share one
    across several servers, or to keep it after the server is gone.

    A response is recorded once it has completed, because its size and duration
    are not known before that. So a client that has already read a body may
    observe the counters a moment before they include it; over a playback the
    difference is one request, and the alternative -- counting responses as they
    start -- would report bytes that had not been sent.

    With ``block=True`` it runs until interrupted; with ``block=False`` it serves
    on a daemon thread, which is what a test wants.
    """
    bundle_dir = Path(bundle_dir).expanduser().resolve()
    index = bundle.read(bundle_dir)
    if not index:
        raise FileNotFoundError(
            f"{bundle_dir} has no {bundle.INDEX_NAME}; export one first"
        )

    # One monitor per server, attached to a subclass rather than to `_Handler`
    # itself: the class attribute is shared, so setting it on the base would
    # make two servers in one process count into each other.
    counters = Monitor() if monitor is None else monitor
    handler_class = type(
        "_BundleHandler",
        (_Handler,),
        {"monitor": counters, "link": link, "upstreams": live.upstreams(index)},
    )
    handler = partial(handler_class, directory=str(bundle_dir))
    try:
        server = _Server((host, port), handler)
    except OSError as error:
        if error.errno != errno.EADDRINUSE:
            raise
        # This box runs several long-lived demo servers in the 87xx range, so a
        # fixed default port collides often enough that failing outright would
        # just be an obstacle. Falling back is visible, since the port is printed.
        server = _Server((host, 0), handler)
        print(f"port {port} is already in use; using {server.server_address[1]} instead")
    server.monitor = counters
    server.link = link
    url = _reachable_address(host, server.server_address[1])

    clips = index.get("clips", [])
    print(f"{index.get('title', bundle_dir.name)}")
    for clip in clips:
        print(
            f"  {clip['name']:<28} {len(clip.get('frames', []))} frames"
            f"  ({clip.get('representation')})"
        )
    print(f"\nserving {bundle_dir}\n  {url}")
    print(f"  link: {described(link)}")
    print(f"  counters at {url.rstrip('/')}{STATS_ROUTE}")
    for name, upstream in live.upstreams(index).items():
        print(f"  live {name} <- {upstream}")
    if host in ("0.0.0.0", "::", ""):
        print("  bound to every interface, with no authentication: anyone who can")
        print("  reach this port can read the bundle.")
    if open_browser:
        webbrowser.open(url)

    if not block:
        # `shutdown()` waits for serve_forever to notice, so the default 0.5 s
        # poll makes every programmatic stop cost half a second -- which a test
        # suite or a measured run pays once per server for nothing.
        threading.Thread(
            target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True
        ).start()
        return server

    print("  Ctrl-C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.shutdown()
        server.server_close()
    return server
