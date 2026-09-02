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

#: Content types for client-package assets. `.wasm` matters: a browser refuses
#: to compile a module served as anything else through the streaming API.
CLIENT_TYPES = {
    ".js": "text/javascript",
    ".wasm": "application/wasm",
    ".html": "text/html; charset=utf-8",
    ".md": "text/plain; charset=utf-8",
}


class _Handler(http.server.SimpleHTTPRequestHandler):
    """Static files from the bundle, plus the client page and the counters.

    ``monitor`` is set per-server by :func:`serve`; a handler class with no
    monitor still works, which is what keeps this usable as a plain static
    server.
    """

    monitor: Monitor | None = None
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
        return super().do_GET()

    def do_HEAD(self):  # noqa: N802
        if self.path in VIEWER_ROUTES:
            return self._send_viewer(body=False)
        return super().do_HEAD()

    def _send_stats(self):
        """What has gone over the wire, as JSON. Absent without a monitor."""
        if self.monitor is None:
            self.send_error(404, "this server keeps no counters")
            return
        payload = json.dumps(self.monitor.snapshot(), indent=2).encode()
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

    def handle_one_request(self):
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
        {"monitor": counters, "upstreams": live.upstreams(index)},
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
    url = _reachable_address(host, server.server_address[1])

    clips = index.get("clips", [])
    print(f"{index.get('title', bundle_dir.name)}")
    for clip in clips:
        print(
            f"  {clip['name']:<28} {len(clip.get('frames', []))} frames"
            f"  ({clip.get('representation')})"
        )
    print(f"\nserving {bundle_dir}\n  {url}")
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
