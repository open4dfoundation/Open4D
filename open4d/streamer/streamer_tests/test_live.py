"""Clips rendered as they are watched.

The transport that makes this a streaming platform for the modules that cannot
be decoded client-side at all, and the only one measured here that fits a link
rather than a LAN: about 47 kB a rendered frame against 4.3 MB for a decoded
Gaussian one.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from streamer import bundle, live
from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")

URL = "http://127.0.0.1:8768/stream"


# --------------------------------------------------------------- the clip ---


def test_a_live_clip_is_same_origin_and_records_its_upstream():
    """The client is handed a path on the bundle server, not the renderer's URL.

    Handing it the renderer's URL asks the *browser* to reach that port. A
    browser on a laptop viewing a tunnelled page cannot, so the pane stays blank
    and nothing reports why -- which is exactly what happened before this.
    """
    clip = live.mjpeg(URL, name="wall", origin="rendered", scene="Vega live wall",
                       method="vega-live")
    assert clip.stream["url"] == "live/wall"
    assert clip.stream["upstream"] == URL
    assert clip.stream["protocol"] == "mjpeg"
    assert clip.frames == []
    assert clip.representation == "pixels"


def test_a_clip_name_must_be_one_path_segment():
    """It becomes a route, so a slash would proxy something else entirely."""
    with pytest.raises(ValueError, match="single path segment"):
        live.mjpeg(URL, name="a/b", origin="rendered")


def test_upstreams_reads_the_mapping_out_of_a_manifest(tmp_path):
    clips = [
        live.mjpeg(URL, name="wall", origin="rendered"),
        live.mjpeg("http://127.0.0.1:8760/stream", name="nevo", origin="replay"),
        bundle.Clip(name="static", representation="pixels", frames=["static/f0.jpg"]),
    ]
    bundle.write(tmp_path, title="t", source="s", clips=clips)
    assert live.upstreams(bundle.read(tmp_path)) == {
        "wall": URL,
        "nevo": "http://127.0.0.1:8760/stream",
    }


def test_it_is_pixels_because_that_is_what_arrives():
    """Whatever the server rendered from, the client receives an image."""
    assert live.mjpeg(URL, name="w", origin="rendered").representation == "pixels"


def test_the_scene_defaults_to_the_clip_name():
    """Its own scene, so Compare never presents it as sharing a rig pose."""
    assert live.mjpeg(URL, name="wall", origin="rendered").scene == "wall"


def test_the_notes_say_it_is_live_and_that_it_is_proxied():
    notes = " ".join(live.mjpeg(URL, name="w", origin="rendered").notes)
    assert "nothing to scrub" in notes
    assert "proxied" in notes
    assert "127.0.0.1:8768" in notes


def test_a_non_http_url_is_refused():
    for bad in ("rtsp://host/stream", "/local/path", "ws://host/s"):
        with pytest.raises(ValueError, match="not an http"):
            live.mjpeg(bad, name="w", origin="rendered")


# ---------------------------------------------------------- the wire format ---


def test_validation_rejects_a_stream_without_a_url():
    with pytest.raises(ValueError, match="needs a url"):
        bundle.validate(
            bundle.Clip(name="w", representation="pixels", stream={"protocol": "mjpeg"})
        )


def test_validation_rejects_a_protocol_the_client_cannot_play():
    """Otherwise it renders nothing, with no error a producer would see."""
    with pytest.raises(ValueError, match="not one the client plays"):
        bundle.validate(
            bundle.Clip(
                name="w",
                representation="pixels",
                stream={"url": URL, "protocol": "webrtc", "origin": "rendered"},
            )
        )


def test_validation_rejects_a_live_clip_that_also_lists_frames():
    with pytest.raises(ValueError, match="no frame list"):
        bundle.validate(
            bundle.Clip(
                name="w",
                representation="pixels",
                frames=["w/f0.jpg"],
                stream={"url": URL, "protocol": "mjpeg", "origin": "rendered"},
            )
        )


def test_validation_rejects_a_clip_with_neither(tmp_path):
    with pytest.raises(ValueError, match="either frames or a stream"):
        bundle.write(
            tmp_path,
            title="t",
            source="s",
            clips=[bundle.Clip(name="empty", representation="pixels")],
        )


def test_a_live_clip_round_trips_through_a_manifest(tmp_path):
    clip = live.mjpeg(URL, name="wall", origin="rendered", scene="Vega live wall")
    bundle.write(tmp_path, title="t", source="s", clips=[clip])
    stored = bundle.read(tmp_path)["clips"][0]
    assert stored["stream"] == {"url": "live/wall", "protocol": "mjpeg",
                                "upstream": URL, "origin": "rendered"}
    assert stored["frames"] == []


def test_live_and_static_clips_coexist_in_one_bundle(tmp_path):
    static = bundle.Clip(
        name="s", representation="pixels", scene="rig", camera=0, frames=["s/f0.jpg"]
    )
    bundle.write(
        tmp_path,
        title="t",
        source="s",
        clips=[static, live.mjpeg(URL, name="wall", origin="rendered", scene="live")],
    )
    stored = bundle.read(tmp_path)["clips"]
    assert [c.get("stream") is not None for c in stored] == [False, True]


# ------------------------------------------------------------- the client ---


def _cut(name: str) -> str:
    page = viewer_path().read_text()
    for prefix in (f"function {name}(", f"const {name} ="):
        start = page.find(prefix)
        if start < 0:
            continue
        line = page[start : page.index("\n", start)]
        if line.count("{") and line.count("{") == line.count("}"):
            return line
        end = (
            page.index(";\n", start) + 2
            if prefix.startswith("const")
            else page.index("\n}\n", start) + 3
        )
        return page[start:end]
    raise AssertionError(f"{name} is not defined in the viewer")


@requires_node
def test_the_client_recognises_a_live_clip(tmp_path):
    script = tmp_path / "live.mjs"
    script.write_text(
        _cut("isLive")
        + """
        process.stdout.write(JSON.stringify([
          isLive({stream: {url: "http://h/s", protocol: "mjpeg"}}),
          isLive({frames: ["a.jpg"]}),
          isLive({}),
          isLive(null),
        ]));
        """
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr
    assert json.loads(finished.stdout) == [True, False, False, False]


def test_a_live_pane_bypasses_the_scheduler():
    """Structural: the browser owns an MJPEG connection, so a cache would fight it."""
    page = viewer_path().read_text()
    start = page.index("  setClip(clip) {")
    body = page[start : page.index("\n  }\n", start)]
    start_of_branch = body.index("if (isLive(clip))")
    # Sliced to where the static path begins, not to the first `return;`. The
    # branch's error handler has an early return of its own, and slicing there
    # cut the assertion's own subject out of the string -- a test that broke on
    # a change it was not about.
    live_branch = body[
        start_of_branch : body.index("const make = kind.renderer;", start_of_branch)
    ]
    assert "this.source = null" in live_branch
    assert "clip.stream.url" in live_branch
    assert "new Scheduler" not in live_branch


def test_the_transport_bar_is_disabled_for_a_live_scene():
    page = viewer_path().read_text()
    start = page.index("function updateTransport(")
    body = page[start : page.index("\n}\n", start)]
    assert "scrubFrame.disabled" in body
    assert "playPause.disabled" in body
    # The readout names what the panes are rather than hardcoding "live", so a
    # replay-only scene does not claim to be live.
    assert "streamLabel(pane.clip)" in body


# ---------------------------------------------------------------- the proxy ---


def upstream_server(payload: bytes, boundary: str = "testframe"):
    """A stand-in renderer: one multipart body that ends, so a test can read it."""
    import http.server
    import socketserver
    import threading

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            self.send_response(200)
            self.send_header(
                "Content-Type", f"multipart/x-mixed-replace; boundary={boundary}"
            )
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, *args):
            pass

    server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(
        target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True
    ).start()
    return server


def test_the_proxy_relays_the_upstream_body(tmp_path):
    import urllib.request

    from streamer.server import serve

    payload = b"--testframe\r\nContent-Type: image/jpeg\r\n\r\n" + bytes(256)
    renderer = upstream_server(payload)
    port = renderer.server_address[1]
    bundle.write(
        tmp_path,
        title="t",
        source="s",
        clips=[live.mjpeg(f"http://127.0.0.1:{port}/stream", name="wall", origin="rendered")],
    )
    server = serve(tmp_path, port=0, block=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        with urllib.request.urlopen(f"{base}/live/wall", timeout=10) as response:
            assert response.status == 200
            assert "multipart/x-mixed-replace" in response.headers["Content-Type"]
            assert response.read() == payload
    finally:
        for each in (server, renderer):
            each.shutdown()
            each.server_close()


def test_an_unknown_live_name_is_a_404(tmp_path):
    import urllib.error
    import urllib.request

    from streamer.server import serve

    bundle.write(
        tmp_path,
        title="t",
        source="s",
        clips=[live.mjpeg("http://127.0.0.1:1/stream", name="wall", origin="rendered")],
    )
    server = serve(tmp_path, port=0, block=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        with pytest.raises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(f"{base}/live/nope", timeout=10)
        assert raised.value.code == 404
    finally:
        server.shutdown()
        server.server_close()


def test_an_unreachable_renderer_is_a_502_naming_the_upstream(tmp_path):
    """The likeliest thing to be wrong, and not the bundle's fault."""
    import urllib.error
    import urllib.request

    from streamer.server import serve

    bundle.write(
        tmp_path,
        title="t",
        source="s",
        clips=[live.mjpeg("http://127.0.0.1:1/stream", name="wall", origin="rendered")],
    )
    server = serve(tmp_path, port=0, block=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        with pytest.raises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(f"{base}/live/wall", timeout=10)
        assert raised.value.code == 502
        assert "127.0.0.1:1" in raised.value.read().decode()
    finally:
        server.shutdown()
        server.server_close()


def test_a_bundle_with_no_live_clips_has_no_proxy_route(tmp_path):
    import urllib.error
    import urllib.request

    from streamer.server import serve

    bundle.write(
        tmp_path,
        title="t",
        source="s",
        clips=[bundle.Clip(name="s", representation="pixels", frames=["s/f0.jpg"])],
    )
    server = serve(tmp_path, port=0, block=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        with pytest.raises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(f"{base}/live/anything", timeout=10)
        assert raised.value.code == 404
    finally:
        server.shutdown()
        server.server_close()


# ------------------------------------------------- live is not the same as replay ---


def test_origin_is_required_so_a_replay_cannot_be_mislabelled_by_omission():
    with pytest.raises(TypeError):
        live.mjpeg(URL, name="w")          # no origin


def test_origin_must_be_one_of_the_two():
    with pytest.raises(ValueError, match="origin must be one of"):
        live.mjpeg(URL, name="w", origin="streaming")


def test_the_notes_distinguish_the_two():
    rendered = " ".join(live.mjpeg(URL, name="w", origin="rendered").notes)
    replay = " ".join(live.mjpeg(URL, name="w", origin="replay").notes)
    assert "decoded and drawn per frame while you watch" in rendered
    assert "nothing is being computed while you watch" in replay
    assert "replay" not in rendered.split("proxied")[0]


def test_validation_rejects_a_stream_with_no_origin():
    """The transport carries both equally, so it cannot be inferred."""
    with pytest.raises(ValueError, match="origin of 'rendered' or 'replay'"):
        bundle.validate(
            bundle.Clip(
                name="w",
                representation="pixels",
                stream={"url": "live/w", "protocol": "mjpeg", "upstream": URL},
            )
        )


@requires_node
def test_the_client_labels_a_replay_as_a_replay(tmp_path):
    script = tmp_path / "label.mjs"
    script.write_text(
        _cut("streamLabel")
        + """
        process.stdout.write(JSON.stringify([
          streamLabel({stream: {origin: "rendered"}}),
          streamLabel({stream: {origin: "replay"}}),
          streamLabel({}),
        ]));
        """
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr
    assert json.loads(finished.stdout) == ["live", "replay", "live"]


# ------------------------------------------------------ attaching to a bundle ---


def a_bundle(root, clips=None):
    from streamer import bundle

    bundle.write(
        root,
        title="t",
        source="s",
        clips=clips if clips is not None else [
            bundle.Clip(name="prepared", representation="mesh", frames=["prepared/f.ply"])
        ],
        fps=24,
        scenes={"basketball": {"poses": []}},
        detail={"sources": [{"exporter": "vega"}]},
    )
    return root


def test_attach_adds_a_live_clip_without_touching_the_others(tmp_path):
    """The workflow: a renderer starts, and joins a bundle exported hours ago."""
    from streamer import bundle, live

    a_bundle(tmp_path)
    live.attach(tmp_path, live.mjpeg(
        "http://127.0.0.1:8802/stream", name="nevo-live", origin="rendered"))

    index = bundle.read(tmp_path)
    names = [clip["name"] for clip in index["clips"]]
    assert names == ["prepared", "nevo-live"]
    assert index["clips"][0]["frames"] == ["prepared/f.ply"]


def test_attach_preserves_the_rest_of_the_manifest(tmp_path):
    """Rewriting the index must not quietly drop the rig or the fps."""
    from streamer import bundle, live

    a_bundle(tmp_path)
    live.attach(tmp_path, live.mjpeg(
        "http://127.0.0.1:8802/stream", name="live", origin="rendered"))
    index = bundle.read(tmp_path)
    assert index["fps"] == 24
    assert "basketball" in index["scenes"]
    assert index["detail"]["sources"][0]["exporter"] == "vega"
    assert index["title"] == "t" and index["source"] == "s"


def test_attach_refuses_to_shadow_a_clip_silently(tmp_path):
    from streamer import live

    a_bundle(tmp_path)
    live.attach(tmp_path, live.mjpeg(
        "http://127.0.0.1:8802/stream", name="live", origin="rendered"))
    with pytest.raises(ValueError, match="already has a clip named"):
        live.attach(tmp_path, live.mjpeg(
            "http://127.0.0.1:9000/stream", name="live", origin="rendered"))


def test_attach_can_replace_a_clip_whose_renderer_moved(tmp_path):
    """Restarting a renderer on a new port is the case this is for."""
    from streamer import bundle, live

    a_bundle(tmp_path)
    live.attach(tmp_path, live.mjpeg(
        "http://127.0.0.1:8802/stream", name="live", origin="rendered"))
    live.attach(tmp_path, live.mjpeg(
        "http://127.0.0.1:9000/stream", name="live", origin="rendered"), replace=True)

    index = bundle.read(tmp_path)
    assert [clip["name"] for clip in index["clips"]] == ["prepared", "live"]
    assert live.upstreams(index) == {"live": "http://127.0.0.1:9000/stream"}


def test_attach_revalidates_every_clip(tmp_path):
    """A rewrite is a write, so the invariants apply to what was already there."""
    from streamer import bundle, live

    a_bundle(tmp_path)
    index = bundle.read(tmp_path)
    # Corrupt an existing clip the way a hand-edited manifest would.
    index["clips"][0]["frames"] = []
    (tmp_path / bundle.INDEX_NAME).write_text(json.dumps(index))

    with pytest.raises(ValueError, match="needs either frames or a stream"):
        live.attach(tmp_path, live.mjpeg(
            "http://127.0.0.1:8802/stream", name="live", origin="rendered"))


def test_attach_needs_a_bundle(tmp_path):
    from streamer import live

    with pytest.raises(FileNotFoundError, match="view.json"):
        live.attach(tmp_path, live.mjpeg(
            "http://127.0.0.1:8802/stream", name="live", origin="rendered"))


# ------------------------------------------------------- many clips at once ---


def test_add_takes_many_clips_in_one_rewrite(tmp_path):
    """A 16-clip export would otherwise rewrite the manifest 16 times, and a
    reader loading it midway would see a partial bundle."""
    a_bundle(tmp_path)
    bundle.add(tmp_path, [
        bundle.Clip(name=f"c{n}", representation="pixels",
                    frames=[f"c{n}/frame_0000.jpg"])
        for n in range(4)
    ])
    names = [clip["name"] for clip in bundle.read(tmp_path)["clips"]]
    assert names == ["prepared", "c0", "c1", "c2", "c3"]


def test_add_refuses_two_incoming_clips_with_one_name(tmp_path):
    """One would silently overwrite the other's entry."""
    a_bundle(tmp_path)
    with pytest.raises(ValueError, match="both named"):
        bundle.add(tmp_path, [
            bundle.Clip(name="same", representation="pixels", frames=["a/f.jpg"]),
            bundle.Clip(name="same", representation="pixels", frames=["b/f.jpg"]),
        ])


# ---------------------------------------------------------- quality rungs ---


def a_variant(name, frames, size):
    return bundle.Variant(name=name, frames=frames, bytes=size).as_dict()


def test_a_clip_can_carry_several_renditions(tmp_path):
    """Inside the clip, not as sibling clips: a consumer has to be able to
    change its mind between them mid-playback, and three clips would be three
    panes with nothing able to switch."""
    a_bundle(tmp_path)
    bundle.add(tmp_path, bundle.Clip(
        name="laddered", representation="pixels", frames=["hi/f0.jpg", "hi/f1.jpg"],
        variants=[a_variant("low", ["lo/f0.jpg", "lo/f1.jpg"], 1000),
                  a_variant("medium", ["md/f0.jpg", "md/f1.jpg"], 4000)],
    ))
    clip = next(c for c in bundle.read(tmp_path)["clips"] if c["name"] == "laddered")
    assert [v.name for v in bundle.variants_of(clip)] == ["low", "medium"]


def test_variants_come_back_cheapest_first(tmp_path):
    """So walking the list is walking the ladder, not the write order."""
    clip = bundle.Clip(name="c", representation="pixels", frames=["a"], variants=[
        a_variant("medium", ["m"], 5000), a_variant("low", ["l"], 900),
        a_variant("high", ["h"], 20000),
    ])
    assert [v.name for v in bundle.variants_of(clip)] == ["low", "medium", "high"]


def test_a_variants_bitrate_is_derived_from_measured_bytes():
    variant = bundle.Variant(name="low", frames=["a", "b", "c", "d"], bytes=12000)
    assert variant.bytes_per_frame == 3000
    assert variant.bitrate(30) == 3000 * 8 * 30


def test_a_clip_with_no_variants_has_one_rendition():
    clip = bundle.Clip(name="c", representation="pixels", frames=["a"])
    assert bundle.variants_of(clip) == ()
    with pytest.raises(KeyError, match="has one rendition"):
        bundle.variant(clip, "low")


def test_asking_for_an_unknown_rung_names_the_known_ones():
    clip = bundle.Clip(name="c", representation="pixels", frames=["a"],
                       variants=[a_variant("low", ["l"], 10)])
    with pytest.raises(KeyError, match="offers low"):
        bundle.variant(clip, "high")


# ------------------------------------------------------- what write refuses ---


def test_renditions_must_share_the_timeline(tmp_path):
    """Something switching at frame n has to land on frame n. A rung with a
    different frame count would make a switch a jump in time."""
    with pytest.raises(ValueError, match="share a timeline"):
        bundle.write(tmp_path, title="t", source="s", clips=[
            bundle.Clip(name="c", representation="pixels",
                        frames=["a/0.jpg", "a/1.jpg", "a/2.jpg"],
                        variants=[a_variant("low", ["b/0.jpg"], 10)]),
        ])


def test_two_rungs_cannot_share_a_name(tmp_path):
    with pytest.raises(ValueError, match="both named"):
        bundle.write(tmp_path, title="t", source="s", clips=[
            bundle.Clip(name="c", representation="pixels", frames=["a/0.jpg"],
                        variants=[a_variant("low", ["b/0.jpg"], 10),
                                  a_variant("low", ["c/0.jpg"], 20)]),
        ])


def test_a_rung_needs_a_name_and_frames(tmp_path):
    for broken, message in (
        ({"frames": ["b/0.jpg"]}, "needs a name"),
        ({"name": "low", "frames": []}, "has no frames"),
    ):
        with pytest.raises(ValueError, match=message):
            bundle.write(tmp_path, title="t", source="s", clips=[
                bundle.Clip(name="c", representation="pixels",
                            frames=["a/0.jpg"], variants=[broken]),
            ])


def test_a_live_clip_cannot_have_rungs(tmp_path):
    """There is no frame list to offer at another quality."""
    clip = live.mjpeg("http://127.0.0.1:9/stream", name="s", origin="rendered")
    clip.variants = [a_variant("low", ["x/0.jpg"], 10)]
    with pytest.raises(ValueError, match="cannot have variants"):
        bundle.write(tmp_path, title="t", source="s", clips=[clip])


def test_an_old_reader_still_plays_a_laddered_clip(tmp_path):
    """The reason `frames` stayed the default rendition instead of moving into
    the variant list: adding rungs must not require changing any consumer."""
    a_bundle(tmp_path)
    bundle.add(tmp_path, bundle.Clip(
        name="laddered", representation="pixels", frames=["hi/f0.jpg"],
        variants=[a_variant("low", ["lo/f0.jpg"], 10)]))
    clip = next(c for c in bundle.read(tmp_path)["clips"] if c["name"] == "laddered")
    # A reader that has never heard of variants sees a perfectly ordinary clip.
    assert clip["frames"] == ["hi/f0.jpg"]
    assert bundle.read(tmp_path)["version"] == bundle.VERSION == 2
