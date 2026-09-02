"""Clips that are rendered as they are watched.

Everything else in a bundle is a directory of frames someone decoded earlier.
That is the wrong shape for two cases this repository already has working, and
both matter:

* **A representation that cannot be decoded in a browser at all.** ReRF's
  entropy coder ships only as a CPython 3.8 binary, so nothing client-side will
  ever read it. Rendering it where the GPU is and sending pixels is not a
  fallback, it is the only transport it has.
* **Bandwidth.** Measured on this repository's own content, one rendered view
  costs about 47 kB a frame against 4.3 MB for a decoded Gaussian frame -- 1.4
  MB/s against 129 MB/s at 30 fps. For a fixed viewpoint, sending pixels wins by
  two orders of magnitude, and it is the only transport here that works over a
  link rather than a LAN.

So a live clip carries a URL instead of a frame list, and the client points an
``<img>`` at it. MJPEG because it needs no JavaScript, no codec and no
negotiation -- ``multipart/x-mixed-replace`` is a browser feature -- and because
`vega.streaming.mjpeg_server` already serves it, which is what makes this
wiring rather than new machinery.

**Live clips get their own scene.** A live renderer chooses its own camera, so
putting one beside a rig pose in Compare would break exactly the guarantee that
mode exists to make: every pane at the same pose. Its own scene means it is
never presented as comparable to something it is not.
"""

from __future__ import annotations

from urllib.parse import urlparse

from . import bundle

#: Path prefix the bundle server proxies live streams under.
ROUTE_PREFIX = "live"


def mjpeg(
    url: str,
    *,
    name: str,
    scene: str | None = None,
    method: str = "live",
    notes: list[str] | None = None,
    detail: dict | None = None,
) -> bundle.Clip:
    """A clip fed by an MJPEG endpoint.

    ``url`` is where the renderer actually is, and it is recorded as the clip's
    *upstream*. What the manifest hands the client is a path on the bundle
    server instead -- ``live/<name>``, which `streamer.server` proxies.

    That indirection is the whole point, and skipping it is a mistake worth
    naming: a manifest that gives the browser ``http://127.0.0.1:8768/stream``
    is telling it to fetch port 8768 *on the machine the browser is running on*.
    Open the page through a tunnel and that is the laptop, which has nothing
    there, so the pane stays blank with no error. Proxying makes the bundle
    server the single origin, so one forwarded port is enough and the bundle
    stays portable.
    """
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https"):
        raise ValueError(f"{url!r} is not an http(s) URL")
    if "/" in name:
        raise ValueError(f"{name!r} must be a single path segment")

    return bundle.validate(
        bundle.Clip(
            name=name,
            representation="pixels",
            scene=scene or name,
            method=method,
            frames=[],
            stream={
                "url": f"{ROUTE_PREFIX}/{name}",
                "protocol": "mjpeg",
                "upstream": url,
            },
            notes=(notes or []) + [
                "live: rendered as it is watched, so there is no frame list and "
                "nothing to scrub",
                f"proxied by the bundle server from {parsed.netloc}, so the page "
                "needs no access to that port itself",
            ],
            detail={**(detail or {}), "upstream": url},
        )
    )


def upstreams(index: dict) -> dict[str, str]:
    """Clip name -> upstream URL, for every live clip in a manifest.

    Read from the manifest rather than passed in separately, so a server started
    against a bundle it did not write proxies exactly what that bundle declares.
    """
    found: dict[str, str] = {}
    for clip in index.get("clips", []):
        stream = clip.get("stream") or {}
        upstream = stream.get("upstream")
        if upstream:
            found[clip["name"]] = upstream
    return found
