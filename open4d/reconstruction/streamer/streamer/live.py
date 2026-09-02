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

    ``url`` is fetched by the *browser*, not by the server that hands out the
    manifest, so it has to be reachable from wherever the page is opened. A
    loopback URL works when the page is opened through a tunnel to the same
    host, which is the normal case here; it does not when the bundle is copied
    to another machine, and that is recorded in the clip's notes rather than
    left to be discovered.
    """
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https"):
        raise ValueError(f"{url!r} is not an http(s) URL")

    reach = (
        f"the browser fetches {parsed.netloc} directly, so this plays only where "
        "that host is reachable from"
    )
    return bundle.validate(
        bundle.Clip(
            name=name,
            representation="pixels",
            scene=scene or name,
            method=method,
            frames=[],
            stream={"url": url, "protocol": "mjpeg"},
            notes=(notes or []) + [
                "live: rendered as it is watched, so there is no frame list and "
                "nothing to scrub",
                reach,
            ],
            detail={**(detail or {}), "endpoint": url},
        )
    )
