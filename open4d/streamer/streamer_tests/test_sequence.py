"""A clip as one file, packed in Python and read in JavaScript.

The container exists because fetching a clip a frame at a time costs a round
trip a frame. On loopback that is invisible -- which is why it survived this
long -- and over a 20 ms link thirty frames is 600 ms of latency before
anything can play.

Two implementations read the layout: `streamer.sequence` and a transcription in
`viewer.html`, because the client cannot run Python. So the middle section of
this file packs a container with the real writer and reads it with the real
reader under Node, over bytes that are not all the same, and holds the two to
each other. A layout the two disagree about is a clip that plays as noise.
"""

from __future__ import annotations

import json
import shutil
import struct
import subprocess
import textwrap
from dataclasses import replace
from pathlib import Path

import pytest

from streamer import bundle, sequence
from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")


def _frames(directory: Path, sizes: list[int], suffix: str = "splat") -> list[Path]:
    """Frame files whose bytes differ, so a misread offset cannot pass.

    Filled with a per-frame byte rather than zeros: a container read with every
    offset shifted by a constant still round-trips identical zeros, and that is
    exactly the bug this is here to catch.
    """
    directory.mkdir(parents=True, exist_ok=True)
    written = []
    for index, size in enumerate(sizes):
        path = directory / f"frame_{index:04d}.{suffix}"
        path.write_bytes(bytes([index + 1]) * size)
        written.append(path)
    return written


# ------------------------------------------------------------ the container ---


def test_a_packed_container_round_trips_every_frame(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 300, 7, 4096])
    header = sequence.pack(frames, tmp_path / "clip.vmesh")

    assert header.frames == 4
    assert header.suffix == "splat"
    assert header.bytes == 10 + 300 + 7 + 4096

    data = (tmp_path / "clip.vmesh").read_bytes()
    for index, path in enumerate(frames):
        assert sequence.frame(data, index, header) == path.read_bytes()


def test_offsets_name_real_vmesh_records(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 300, 7])
    header = sequence.pack(frames, tmp_path / "clip.vmesh")
    data = (tmp_path / "clip.vmesh").read_bytes()
    assert data[:8] == b"VMESH\x00\x01\x00"
    first = header.entries[0]
    assert first.offset == 26 + struct.unpack_from(">I", data, 8)[0] - 14
    for before, after in zip(header.entries, header.entries[1:]):
        assert before.offset + before.length + 18 == after.offset
    last = header.entries[-1]
    assert last.offset + last.length + 18 == header.end_offset
    assert header.end_offset + 50 == len(data)


def test_a_header_read_back_matches_the_one_written(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 300, 7])
    written = sequence.pack(frames, tmp_path / "clip.vmesh")
    read = sequence.read_header((tmp_path / "clip.vmesh").read_bytes())
    assert read == written


def test_the_header_can_be_read_before_the_body_arrives(tmp_path):
    frames = _frames(tmp_path / "clip", [4096, 4096, 4096])
    written = sequence.pack(frames, tmp_path / "clip.vmesh")
    # The point of a fixed header: a progress count knows how many frames are
    # coming from the first few hundred bytes, not the last.
    prefix = (tmp_path / "clip.vmesh").read_bytes()[: written.entries[0].offset]
    assert sequence.read_header(prefix) == written


def test_unpacking_restores_the_frames(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 300, 7])
    sequence.pack(frames, tmp_path / "clip.vmesh")
    restored = sequence.unpack(tmp_path / "clip.vmesh", tmp_path / "out")
    assert [path.read_bytes() for path in restored] == [
        path.read_bytes() for path in frames
    ]


# ------------------------------------------------- what it refuses to accept ---


def test_a_container_needs_a_frame(tmp_path):
    with pytest.raises(ValueError, match="at least one frame"):
        sequence.pack([], tmp_path / "clip.vmesh")


def test_frames_must_share_a_suffix(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 10])
    odd = tmp_path / "clip" / "frame_0002.ply"
    odd.write_bytes(b"x" * 10)
    # Two formats in one container would need the client to switch decoders
    # mid-clip. A clip whose frames are not one codec is two clips.
    with pytest.raises(ValueError, match="share one suffix"):
        sequence.pack(frames + [odd], tmp_path / "clip.vmesh")


def test_a_missing_frame_is_named(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 10])
    with pytest.raises(FileNotFoundError, match="frame_0009"):
        sequence.pack(
            frames + [tmp_path / "clip" / "frame_0009.splat"], tmp_path / "clip.vmesh"
        )


@pytest.mark.parametrize(
    "mangle, complaint",
    [
        (lambda data: b"NOTASEQ\x00" + data[8:], "magic/version"),
        (lambda data: data[:12] + b"\x63" + data[13:], "record version/type"),
        (lambda data: data[:12], "truncated stream"),
        (lambda data: data[:24], "truncated stream"),
    ],
)
def test_a_damaged_header_is_refused(tmp_path, mangle, complaint):
    sequence.pack(_frames(tmp_path / "clip", [4096] * 8), tmp_path / "clip.vmesh")
    with pytest.raises(ValueError, match=complaint):
        sequence.read_header(mangle((tmp_path / "clip.vmesh").read_bytes()))


# ---------------------------------------------------------- packing a clip ---


def _clip(root: Path, sizes: list[int], suffix: str = "splat") -> bundle.Clip:
    paths = _frames(root / "clip", sizes, suffix)
    return bundle.Clip(
        name="clip",
        representation="gaussians" if suffix == "splat" else "pixels",
        frames=[str(path.relative_to(root)) for path in paths],
    )


def test_packing_a_clip_records_where_it_went(tmp_path):
    clip = _clip(tmp_path, [10, 300, 7])
    packed = sequence.pack_clip(tmp_path, clip)

    assert packed["url"] == "clip.vmesh"
    assert packed["frames"] == 3
    assert packed["suffix"] == "splat"
    assert packed["bytes"] == (tmp_path / "clip.vmesh").stat().st_size
    # Taken from the registry rather than mapped again in the client: the
    # container is served as octet-stream, so the frame's own type has to come
    # from the manifest or from nowhere.
    assert packed["media_type"] == "application/octet-stream"


def test_a_packed_image_clip_carries_the_frame_type(tmp_path):
    clip = _clip(tmp_path, [10, 10], suffix="jpg")
    assert sequence.pack_clip(tmp_path, clip)["media_type"] == "image/jpeg"


def test_packing_removes_the_frames_it_replaced(tmp_path):
    clip = _clip(tmp_path, [10, 300, 7])
    sequence.pack_clip(tmp_path, clip)
    # Both would double the bundle on disk for nothing: a client handed a
    # container never asks for the pieces.
    assert not (tmp_path / "clip").exists()
    assert (tmp_path / "clip.vmesh").is_file()


def test_packing_can_keep_the_frames(tmp_path):
    clip = _clip(tmp_path, [10, 300, 7])
    sequence.pack_clip(tmp_path, clip, keep_frames=True)
    assert all((tmp_path / frame).is_file() for frame in clip.frames)


def test_a_packed_clip_still_validates(tmp_path):
    clip = _clip(tmp_path, [10, 300, 7])
    packed = sequence.pack_clip(tmp_path, clip)
    bundle.validate(replace(clip, sequence=packed))


def test_a_sequence_must_agree_with_the_frame_list(tmp_path):
    clip = _clip(tmp_path, [10, 300, 7])
    packed = sequence.pack_clip(tmp_path, clip)
    packed["frames"] = 2
    # Disagreement here means the player and the container index different
    # instants, and a seek silently lands on the wrong picture.
    with pytest.raises(ValueError, match="2 frames against the clip's 3"):
        bundle.validate(replace(clip, sequence=packed))


def test_a_sequence_needs_a_url(tmp_path):
    clip = _clip(tmp_path, [10])
    with pytest.raises(ValueError, match="needs a url"):
        bundle.validate(replace(clip, sequence={"frames": 1}))


def test_a_live_clip_cannot_be_packed(tmp_path):
    live = bundle.Clip(
        name="live",
        representation="pixels",
        frames=[],
        stream={"url": "/live", "protocol": "mjpeg", "origin": "rendered"},
    )
    # Nothing to pack: the pixels do not exist until someone is watching.
    with pytest.raises(ValueError, match="no end to pack"):
        bundle.validate(replace(live, sequence={"url": "live.vmesh", "frames": 0}))


# ------------------------------------------------- one layout, two languages ---


def _extract(*names: str) -> str:
    """Named top-level definitions, cut out of the page as shipped."""
    page = viewer_path().read_text()
    chunks = []
    for name in names:
        for prefix in (f"function {name}(", f"class {name} ", f"const {name} ="):
            start = page.find(prefix)
            if start >= 0:
                break
        else:
            raise AssertionError(f"{name} is not defined in the viewer")
        end = (
            page.index(";\n", start) + 2
            if prefix.startswith("const")
            else page.index("\n}\n", start) + 3
        )
        chunks.append(page[start:end])
    return "\n".join(chunks)


READER = ("SEQ_MAGIC", "SEQ_PREAMBLE", "VMESH_CHUNK", "vmeshSHA256",
          "vmeshJSON", "vmeshRecord", "readSequenceHeader", "validateSequenceEnd", "sequenceFrame")


def run_js(body: str, tmp_path: Path, names=READER, name: str = "q.mjs") -> object:
    """Run ``body`` with the page's container code in scope; return its JSON."""
    script = tmp_path / name
    script.write_text(_extract(*names) + "\n" + textwrap.dedent(body))
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    if finished.returncode:
        raise AssertionError(finished.stderr)
    return json.loads(finished.stdout)


LOAD = """
    import {readFileSync} from "node:fs";
    const load = (path) => {
      const bytes = readFileSync(path);
      return bytes.buffer.slice(
        bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
    };
"""


@requires_node
def test_the_client_reads_a_header_python_wrote(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 300, 7, 4096])
    header = sequence.pack(frames, tmp_path / "clip.vmesh")

    result = run_js(
        LOAD
        + f"""
        const header = readSequenceHeader(load({str(tmp_path / "clip.vmesh")!r}));
        process.stdout.write(JSON.stringify(header));
    """,
        tmp_path,
    )
    assert result["suffix"] == header.suffix
    assert result["entries"] == [
        {"offset": entry.offset, "length": entry.length, "sha256": entry.sha256}
        for entry in header.entries
    ]


@requires_node
def test_the_client_slices_the_same_bytes(tmp_path):
    # Sizes chosen unequal and unaligned: equal-length frames would let an
    # off-by-one in the offset table pass every frame but the last.
    frames = _frames(tmp_path / "clip", [10, 300, 7, 4096, 33])
    sequence.pack(frames, tmp_path / "clip.vmesh")

    result = run_js(
        LOAD
        + f"""
        const buffer = load({str(tmp_path / "clip.vmesh")!r});
        const header = readSequenceHeader(buffer);
        const out = [];
        for (let i = 0; i < header.entries.length; i++) {{
          const bytes = new Uint8Array(sequenceFrame(buffer, header, i));
          // Length and content, summarised: every byte of a frame is the same
          // value, so first and last catch a slice that straddled a boundary.
          out.push([bytes.length, bytes[0], bytes[bytes.length - 1]]);
        }}
        process.stdout.write(JSON.stringify(out));
    """,
        tmp_path,
    )
    assert result == [
        [len(path.read_bytes()), index + 1, index + 1]
        for index, path in enumerate(frames)
    ]


@requires_node
def test_a_sliced_frame_does_not_alias_the_container(tmp_path):
    sequence.pack(_frames(tmp_path / "clip", [64, 64]), tmp_path / "clip.vmesh")
    result = run_js(
        LOAD
        + f"""
        const buffer = load({str(tmp_path / "clip.vmesh")!r});
        const header = readSequenceHeader(buffer);
        const first = sequenceFrame(buffer, header, 0);
        new Uint8Array(first).fill(0xff);
        // A view would have written through. The decode worker takes ownership
        // of what it is handed, so a view would transfer the whole container on
        // the first frame and leave every later one unreachable.
        const container = new Uint8Array(buffer);
        process.stdout.write(JSON.stringify({{
          inContainer: container[header.entries[0].offset + 18],
          inFrame: new Uint8Array(first)[0],
        }}));
    """,
        tmp_path,
    )
    assert result == {"inContainer": 1, "inFrame": 255}


# --------------------------------------------------- the scheduler's transfer ---
# The container's whole point is the request count, so that is what these
# assert on: one request for a clip, not one per frame.

SCHEDULER = ("INDEPENDENT", "dependencyOf", "chain", "suffixOf", *READER, "Scheduler")

HARNESS = """
    import {readFileSync} from "node:fs";
    globalThis.fetched = [];
    globalThis.serve = (path) => {
      globalThis.fetch = (url) => {
        globalThis.fetched.push(url);
        const bytes = readFileSync(path);
        const buffer = bytes.buffer.slice(
          bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
        return Promise.resolve({ok: true, arrayBuffer: () => Promise.resolve(buffer)});
      };
    };
    const packedClip = (frames, seq) => ({
      frames: Array.from({length: frames}, (_, i) => `f${i}.splat`),
      dependency: null,
      sequence: seq,
    });
    // Records the length it was given, so a test can tell frames apart without
    // a real decoder.
    const decode = (buffer, url, mediaType) => ({
      bytes: buffer.byteLength, first: new Uint8Array(buffer)[0], mediaType,
    });
"""


@requires_node
def test_a_packed_clip_is_fetched_in_one_request(tmp_path):
    frames = _frames(tmp_path / "clip", [10, 300, 7, 4096])
    sequence.pack(frames, tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 4, "bytes":
              (tmp_path / "clip.vmesh").stat().st_size,
              "media_type": "application/octet-stream"}

    result = run_js(
        HARNESS
        + f"""
        globalThis.serve({str(tmp_path / "clip.vmesh")!r});
        const s = new Scheduler(packedClip(4, {json.dumps(packed)}), "", decode,
                                {{cacheSize: 1}});
        (async () => {{
          const held = await s.downloadAll(null);
          const seen = [];
          for (let i = 0; i < 4; i++) seen.push(await s.seek(i));
          process.stdout.write(JSON.stringify({{
            held, seen, requests: globalThis.fetched, snap: s.snapshot(),
          }}));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )

    assert result["requests"] == ["clip.vmesh"]
    assert result["held"] is True
    assert [frame["bytes"] for frame in result["seen"]] == [10, 300, 7, 4096]
    # Each frame's bytes are all its own index, so this catches a slice that
    # read the right length from the wrong place.
    assert [frame["first"] for frame in result["seen"]] == [1, 2, 3, 4]
    # From the manifest: the container is octet-stream, so there is no response
    # header carrying the frame's type.
    assert {frame["mediaType"] for frame in result["seen"]} == {
        "application/octet-stream"
    }
    # cacheSize was 1 and the whole clip is held anyway -- downloading raises
    # the cache to fit, because a resident clip that evicts is not resident.
    assert result["snap"]["cached"] == 4
    assert result["snap"]["hits"] == 4
    assert result["snap"]["misses"] == 0


@requires_node
def test_progress_climbs_to_the_frame_count(tmp_path):
    sequence.pack(_frames(tmp_path / "clip", [4096] * 6), tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 6,
              "bytes": (tmp_path / "clip.vmesh").stat().st_size}

    result = run_js(
        HARNESS
        + f"""
        globalThis.serve({str(tmp_path / "clip.vmesh")!r});
        const s = new Scheduler(packedClip(6, {json.dumps(packed)}), "", decode, {{}});
        (async () => {{
          const seen = [];
          await s.downloadAll((got, of, bytes, holding) =>
            seen.push([got, of, holding]));
          process.stdout.write(JSON.stringify(seen));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )

    # A bar that goes backwards reads as a broken download, so the two phases
    # -- bytes arriving, then frames decoding -- have to share one rising scale.
    counts = [step[0] for step in result]
    assert counts == sorted(counts)
    assert counts[-1] == 6
    assert all(step[1] == 6 and step[2] is True for step in result)


@requires_node
def test_a_container_too_big_to_hold_is_refused_unfetched(tmp_path):
    sequence.pack(_frames(tmp_path / "clip", [4096] * 4), tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 4, "bytes": 900e6}

    result = run_js(
        HARNESS
        + f"""
        globalThis.serve({str(tmp_path / "clip.vmesh")!r});
        const s = new Scheduler(packedClip(4, {json.dumps(packed)}), "", decode, {{}});
        (async () => {{
          const held = await s.downloadAll(null);
          process.stdout.write(JSON.stringify({{
            held, requests: globalThis.fetched, resident: s.resident,
            cacheSize: s.cacheSize,
          }}));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )

    # Known from the manifest, so nothing is transferred to find it out. The
    # per-frame path can only notice once it has already paid.
    assert result["requests"] == []
    assert result["held"] is False
    assert result["resident"] is False
    assert result["cacheSize"] == 6


@requires_node
def test_a_container_that_disagrees_with_the_clip_is_an_error(tmp_path):
    sequence.pack(_frames(tmp_path / "clip", [4096] * 4), tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 5,
              "bytes": (tmp_path / "clip.vmesh").stat().st_size}

    result = run_js(
        HARNESS
        + f"""
        globalThis.serve({str(tmp_path / "clip.vmesh")!r});
        const s = new Scheduler(packedClip(5, {json.dumps(packed)}), "", decode, {{}});
        (async () => {{
          let message = null;
          try {{ await s.downloadAll(null); }} catch (e) {{ message = e.message; }}
          process.stdout.write(JSON.stringify({{message}}));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )
    # Loudly, rather than playing four frames and looping early: the manifest
    # and the container disagreeing is a build that went wrong.
    assert "holds 4 frames" in result["message"]
    assert "lists 5" in result["message"]


@requires_node
def test_seeking_a_packed_clip_pulls_the_container_once(tmp_path):
    sequence.pack(_frames(tmp_path / "clip", [4096] * 4), tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 4,
              "bytes": (tmp_path / "clip.vmesh").stat().st_size}

    result = run_js(
        HARNESS
        + f"""
        globalThis.serve({str(tmp_path / "clip.vmesh")!r});
        const s = new Scheduler(packedClip(4, {json.dumps(packed)}), "", decode, {{}});
        (async () => {{
          // Two seeks started together, before either could finish. Packing
          // deletes the frame files, so a seek has nowhere else to go -- and
          // two concurrent ones must not both pull the container.
          const frames = await Promise.all([s.seek(2), s.seek(3)]);
          process.stdout.write(JSON.stringify({{
            firsts: frames.map((f) => f.first), requests: globalThis.fetched,
          }}));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )
    assert result["requests"] == ["clip.vmesh"]
    assert result["firsts"] == [3, 4]


@requires_node
def test_an_unpacked_clip_still_fetches_frame_by_frame(tmp_path):
    result = run_js(
        HARNESS
        + """
        globalThis.fetch = (url) => {
          globalThis.fetched.push(url);
          return Promise.resolve({
            ok: true, arrayBuffer: () => Promise.resolve(new ArrayBuffer(16)),
          });
        };
        const s = new Scheduler(packedClip(3, null), "", decode, {});
        (async () => {
          await s.downloadAll(null);
          process.stdout.write(JSON.stringify(globalThis.fetched));
        })();
    """,
        tmp_path,
        names=SCHEDULER,
    )
    # The old path is still the path for a bundle that was never packed, and
    # this is what says so.
    assert result == ["f0.splat", "f1.splat", "f2.splat"]


@requires_node
def test_a_rung_without_a_container_is_not_served_the_base_one(tmp_path):
    sequence.pack(_frames(tmp_path / "clip", [4096] * 3), tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 3,
              "bytes": (tmp_path / "clip.vmesh").stat().st_size}

    result = run_js(
        HARNESS
        + f"""
        globalThis.fetch = (url) => {{
          globalThis.fetched.push(url);
          return Promise.resolve({{
            ok: true, arrayBuffer: () => Promise.resolve(new ArrayBuffer(16)),
          }});
        }};
        const s = new Scheduler(packedClip(3, {json.dumps(packed)}), "", decode, {{}});
        s.useRung({{name: "low", frames: ["lo0.splat", "lo1.splat", "lo2.splat"]}});
        (async () => {{
          await s.downloadAll(null);
          process.stdout.write(JSON.stringify(globalThis.fetched));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )
    # The base container holds the right frames at the wrong quality. Reading
    # it for a rung that has none would silently serve the rung above.
    assert result == ["lo0.splat", "lo1.splat", "lo2.splat"]


# ------------------------------------------------ decoded under its own name ---
# Reported as "why is vega blank". A frame is decoded by the codec its *name*
# selects (`worker.js` sniffs the suffix), so handing the decode the
# container's name means a `.vmesh` in a table of `.ply` and `.splat`. Every
# Gaussian pane went blank; the pixel panes did not, because an image is
# decoded from its media type on the main thread -- so it looked like one
# method being broken rather than one code path.


@requires_node
def test_a_packed_frame_is_decoded_under_its_own_name(tmp_path):
    sequence.pack(_frames(tmp_path / "clip", [64] * 3), tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 3,
              "bytes": (tmp_path / "clip.vmesh").stat().st_size}

    result = run_js(
        HARNESS
        + f"""
        globalThis.serve({str(tmp_path / "clip.vmesh")!r});
        // The real rule, from worker.js: the codec comes from the suffix.
        const CODECS = {{gaussians: {{".ply": 1, ".splat": 1}}}};
        const pick = (url) => {{
          const path = url.split("?", 1)[0];
          const suffix = path.slice(path.lastIndexOf(".")).toLowerCase();
          if (!CODECS.gaussians[suffix]) {{
            throw new Error(`no decoder for a gaussians frame ending ${{suffix}}`);
          }}
          return suffix;
        }};
        const s = new Scheduler(packedClip(3, {json.dumps(packed)}), "./",
                                (buffer, url) => ({{url, suffix: pick(url)}}), {{}});
        (async () => {{
          let message = null, seen = [];
          try {{
            await s.downloadAll(null);
            for (let i = 0; i < 3; i++) seen.push((await s.seek(i)).url);
          }} catch (e) {{ message = e.message; }}
          process.stdout.write(JSON.stringify({{message, seen}}));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )
    assert result["message"] is None, result["message"]
    # The frame's own path, exactly as the unpacked path passes it -- so the
    # two share one decode behaviour rather than having two to keep in step.
    assert result["seen"] == ["./f0.splat", "./f1.splat", "./f2.splat"]


@requires_node
def test_a_container_whose_frames_are_a_different_format_is_refused(tmp_path):
    # A `.jpg` container against a clip listing `.splat` frames. Both are
    # plausible files and the length check passes, so without this the frames
    # would be handed to the Gaussian parser and come out as noise.
    sequence.pack(_frames(tmp_path / "clip", [64] * 3, suffix="jpg"),
                  tmp_path / "clip.vmesh")
    packed = {"url": "clip.vmesh", "frames": 3,
              "bytes": (tmp_path / "clip.vmesh").stat().st_size}

    result = run_js(
        HARNESS
        + f"""
        globalThis.serve({str(tmp_path / "clip.vmesh")!r});
        const s = new Scheduler(packedClip(3, {json.dumps(packed)}), "./", decode, {{}});
        (async () => {{
          let message = null;
          try {{ await s.downloadAll(null); }} catch (e) {{ message = e.message; }}
          process.stdout.write(JSON.stringify({{message}}));
        }})();
    """,
        tmp_path,
        names=SCHEDULER,
    )
    assert "holds .jpg frames" in result["message"]
    assert "lists .splat" in result["message"]


def test_the_viewer_and_the_worker_agree_on_a_suffix():
    """`suffixOf` exists in both, and a frame's codec depends on them agreeing.

    The worker is a separate script, so the rule is transcribed rather than
    shared -- which is exactly the kind of duplication that drifts. Held to the
    original over the cases that distinguish plausible implementations.
    """
    from streamer.client import viewer_path

    page = viewer_path().read_text()
    worker = (viewer_path().parent / "worker.js").read_text()

    def cut(source: str) -> str:
        start = source.index("function suffixOf(")
        return source[start:source.index("\n}\n", start) + 3]

    body = (
        cut(page)
        + cut(worker).replace("function suffixOf(", "function original(")
        + """
        const cases = ["a/b.splat", "a/b.SPLAT", "a/b.ply?v=2", "b.tar.gz",
                       "nodot", "a.b/c", "", "a/b.vmesh"];
        const rows = cases.map((c) => [c, suffixOf(c), original(c)]);
        process.stdout.write(JSON.stringify(rows));
        """
    )
    finished = subprocess.run([NODE, "--input-type=module", "-e", body],
                              capture_output=True, text=True, timeout=60)
    if finished.returncode:
        raise AssertionError(finished.stderr)
    rows = json.loads(finished.stdout)
    disagree = [row for row in rows if row[1] != row[2]]
    assert not disagree, f"suffixOf disagrees with the worker on {disagree}"
    # And the case that started this: a container is not a frame format.
    assert dict((row[0], row[1]) for row in rows)["a/b.vmesh"] == ".vmesh"


def test_a_failed_download_is_explained_in_the_pane():
    """Not left blank.

    The decoder error propagated out of `downloadSelection` as an unhandled
    rejection: the pane stayed empty, nothing surfaced where anyone would look,
    and the page went on reporting success. A pane that cannot show its content
    has to say why -- and the panes beside it have to keep going.
    """
    from streamer.client import viewer_path

    page = viewer_path().read_text()
    start = page.index("async function downloadSelection(")
    body = page[start:page.index("\n}\n", start)]
    assert "catch (error)" in body
    assert "_explain(" in body
    # Carries on rather than abandoning the remaining panes.
    assert "continue;" in body
def test_failed_bundle_pack_keeps_original_frames_and_manifest(tmp_path, monkeypatch):
    clips = []
    for name in ("first", "second"):
        paths = _frames(tmp_path / name, [12, 12])
        clips.append(bundle.Clip(name=name, representation="gaussians",
                                 frames=[str(p.relative_to(tmp_path)) for p in paths]))
    bundle.write(tmp_path, title="test", source="test", clips=clips)
    before = (tmp_path / "view.json").read_bytes()
    original = sequence.pack_clip

    def fail_second(root, clip, **kwargs):
        if clip.name == "second":
            raise OSError("simulated packing failure")
        return original(root, clip, **kwargs)

    monkeypatch.setattr(sequence, "pack_clip", fail_second)
    with pytest.raises(OSError):
        sequence.pack_bundle(tmp_path)
    assert (tmp_path / "view.json").read_bytes() == before
    assert all((tmp_path / path).is_file() for clip in clips for path in clip.frames)


def test_packing_keeps_frames_referenced_by_variants_or_unpacked_clips(tmp_path):
    paths = _frames(tmp_path / "shared", [12, 12])
    frames = [str(p.relative_to(tmp_path)) for p in paths]
    clips = [bundle.Clip(name="first", representation="gaussians", frames=frames,
                         variants=[{"name": "original", "frames": frames}]),
             bundle.Clip(name="second", representation="gaussians", frames=frames)]
    bundle.write(tmp_path, title="test", source="test", clips=clips)
    sequence.pack_bundle(tmp_path, names=["first"])
    assert all(p.is_file() for p in paths)


def test_browser_vmesh_is_validated_by_shared_codec_inspector(tmp_path):
    from open4d.codec import inspect_vmesh
    paths = _frames(tmp_path / "input", [13, 1_048_581], suffix="drc")
    target = tmp_path / "clip.vmesh"
    header = sequence.pack(paths, target, fps=24)
    descriptor = inspect_vmesh(target)
    assert descriptor["codec"] == "frames"
    assert descriptor["native"] == {"profile": "frames/1", "suffix": "drc", "representation": "mesh"}
    assert descriptor["sequence"]["frames"][1]["timestamp"] == 1 / 24
    assert all(sequence.frame(target.read_bytes(), i, header) == path.read_bytes()
               for i, path in enumerate(paths))


@pytest.mark.parametrize("suffix", ["seq", "o4d", "bin"])
def test_only_vmesh_is_a_public_sequence_output(tmp_path, suffix):
    with pytest.raises(ValueError, match=".vmesh extension"):
        sequence.pack(_frames(tmp_path / "input", [12]), tmp_path / f"clip.{suffix}")


@pytest.mark.parametrize("fps", [0, -1, float("nan"), float("inf"), True])
def test_sequence_timing_must_be_finite_positive(tmp_path, fps):
    with pytest.raises(ValueError, match="fps"):
        sequence.pack(_frames(tmp_path / "input", [12]), tmp_path / "clip.vmesh", fps=fps)


def _legacy_bytes(payloads=(b"abc", b"defg"), suffix="splat"):
    header = struct.pack("<8sIII", b"O4DSEQ\x00\x00", 1, len(payloads), len(suffix))
    at = 20 + len(suffix) + 8 * len(payloads)
    table = bytearray()
    for payload in payloads:
        table.extend(struct.pack("<II", at, len(payload)))
        at += len(payload)
    return header + suffix.encode() + table + b"".join(payloads)


def test_legacy_conversion_is_explicit_and_preserves_payloads(tmp_path):
    source, target = tmp_path / "old.seq", tmp_path / "new.vmesh"
    data = _legacy_bytes()
    source.write_bytes(data)
    with pytest.raises(ValueError, match="magic/version"):
        sequence.read_header(data)
    assert sequence.convert_legacy(source, target, fps=12) == target
    header = sequence.read_header(target.read_bytes())
    assert [sequence.frame(target.read_bytes(), i, header) for i in range(2)] == [b"abc", b"defg"]
    assert source.read_bytes() == data


@pytest.mark.parametrize("mutate", [
    lambda data: data[:-1],
    lambda data: data + b"extra",
    lambda data: data[:12] + struct.pack("<I", 2**32 - 1) + data[16:],
    lambda data: data[:25] + struct.pack("<I", 0) + data[29:],
])
def test_legacy_conversion_refuses_bad_bounds_before_publication(tmp_path, mutate):
    source, target = tmp_path / "old.seq", tmp_path / "new.vmesh"
    source.write_bytes(mutate(_legacy_bytes()))
    with pytest.raises(ValueError):
        sequence.convert_legacy(source, target)
    assert not target.exists()


def test_corrupt_payload_is_not_extracted(tmp_path):
    target = tmp_path / "clip.vmesh"
    header = sequence.pack(_frames(tmp_path / "input", [64, 64]), target)
    data = bytearray(target.read_bytes())
    data[header.entries[0].offset + 18] ^= 1
    target.write_bytes(data)
    with pytest.raises(ValueError, match="SHA-256"):
        sequence.frame(data, 0)
    with pytest.raises(ValueError, match="SHA-256"):
        sequence.unpack(target, tmp_path / "output")
    assert not (tmp_path / "output").exists()


@requires_node
@pytest.mark.parametrize("size", [0, 3, 55, 56, 64, 1_048_581])
def test_browser_sha256_matches_native_sha256(tmp_path, size):
    import hashlib
    data = bytes((i * 37) % 256 for i in range(size))
    source = tmp_path / "payload.bin"
    source.write_bytes(data)
    result = run_js(LOAD + f"process.stdout.write(JSON.stringify(vmeshSHA256(load({str(source)!r}))));", tmp_path)
    assert result == hashlib.sha256(data).hexdigest()


@requires_node
def test_browser_reassembles_large_multirecord_frames(tmp_path):
    target = tmp_path / "clip.vmesh"
    sequence.pack(_frames(tmp_path / "input", [1_048_581, 17]), target)
    result = run_js(LOAD + f"""
      const buffer = load({str(target)!r}), header = readSequenceHeader(buffer);
      validateSequenceEnd(buffer, header);
      process.stdout.write(JSON.stringify(header.entries.map((entry, i) => {{
        const bytes = new Uint8Array(sequenceFrame(buffer, header, i));
        return [bytes.length, bytes[0], bytes[bytes.length - 1]];
      }})));
    """, tmp_path)
    assert result == [[1_048_581, 1, 1], [17, 2, 2]]


@requires_node
@pytest.mark.parametrize("corruption", ["payload", "record", "end", "trailing", "legacy"])
def test_browser_refuses_corrupt_vmesh_before_decoder(tmp_path, corruption):
    target = tmp_path / "clip.vmesh"
    header = sequence.pack(_frames(tmp_path / "input", [64, 64]), target)
    data = bytearray(target.read_bytes())
    if corruption == "payload":
        data[header.entries[0].offset + 18] ^= 1
    elif corruption == "record":
        data[header.entries[0].offset + 5] = 2
    elif corruption == "end":
        data[-1] ^= 1
    elif corruption == "trailing":
        data += b"extra"
    else:
        data = _legacy_bytes()
    target.write_bytes(data)
    result = run_js(LOAD + f"""
      let message = null;
      try {{
        const buffer = load({str(target)!r}), header = readSequenceHeader(buffer);
        validateSequenceEnd(buffer, header);
        sequenceFrame(buffer, header, 0);
      }} catch (error) {{ message = error.message; }}
      process.stdout.write(JSON.stringify(message));
    """, tmp_path)
    assert result


def test_public_frame_loader_preserves_mesh_geometry_and_timestamps(tmp_path):
    import numpy as np
    from open4d.io._mesh import write_ply
    paths = []
    positions = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32)
    triangles = np.array([[0, 1, 2]], dtype=np.uint32)
    for index in range(2):
        path = tmp_path / f"frame_{index}.ply"
        write_ply(path, positions + index, triangles)
        paths.append(path)
    target = tmp_path / "mesh.vmesh"
    sequence.pack(paths, target, fps=12, representation="mesh")
    with sequence.open_frames(target) as restored:
        assert restored.timestamps == (0, 1 / 12)
        assert [frame.frame_index for frame in restored] == [0, 1]
        np.testing.assert_array_equal(restored[1].geometry.positions, positions + 1)
        np.testing.assert_array_equal(restored[0].geometry.triangles, triangles)


def test_public_frame_loader_preserves_images_without_deserialization(tmp_path):
    from open4d.native import NativeSequence
    target = tmp_path / "pixels.vmesh"
    sequence.pack(_frames(tmp_path / "input", [23, 35], suffix="png"), target)
    with sequence.open_frames(target) as restored:
        assert isinstance(restored, NativeSequence)
        assert restored.codec == "frames"
        assert restored.manifest["native"]["representation"] == "pixels"
        assert len(restored) == 2


def test_packing_a_bundle_uses_its_real_fps_and_representation(tmp_path):
    from open4d.codec import inspect_vmesh
    clip = _clip(tmp_path, [23, 35])
    bundle.write(tmp_path, title="test", source="test", clips=[clip], fps=24)
    sequence.pack_bundle(tmp_path)
    manifest = inspect_vmesh(tmp_path / "clip.vmesh")
    assert manifest["sequence"]["frames"][1]["timestamp"] == 1 / 24
    assert manifest["native"]["representation"] == "gaussians"


@pytest.mark.parametrize("representation", ["unknown", "pixels", "points"])
def test_frame_suffix_must_match_its_representation(tmp_path, representation):
    with pytest.raises(ValueError, match="representation"):
        sequence.pack(_frames(tmp_path / "input", [12]), tmp_path / "bad.vmesh", representation=representation)


@requires_node
@pytest.mark.parametrize("text", [
    '{"codec":"frames","codec":"frames"}',
    '{"nested":{"name":1,"na\\u006de":2}}',
    '{"a":[{"x":1,"x":2}]}',
])
def test_browser_refuses_duplicate_json_keys(tmp_path, text):
    result = run_js(f"""
      let message = null;
      try {{ vmeshJSON({json.dumps(text)}); }} catch (error) {{ message = error.message; }}
      process.stdout.write(JSON.stringify(message));
    """, tmp_path)
    assert "duplicate" in result


@requires_node
def test_browser_partial_download_can_recover_first_complete_frame(tmp_path):
    target = tmp_path / "clip.vmesh"
    header = sequence.pack(_frames(tmp_path / "input", [71, 103]), target)
    partial = tmp_path / "partial.bin"
    partial.write_bytes(target.read_bytes()[:header.entries[1].offset])
    result = run_js(LOAD + f"""
      const buffer = load({str(partial)!r}), header = readSequenceHeader(buffer);
      const frame = new Uint8Array(sequenceFrame(buffer, header, 0));
      let unavailable = false;
      try {{ sequenceFrame(buffer, header, 1); }} catch (_) {{ unavailable = true; }}
      process.stdout.write(JSON.stringify([frame.length, frame[0], unavailable]));
    """, tmp_path)
    assert result == [71, 1, True]


def test_existing_packed_bundle_migrates_and_rewrites_its_url(tmp_path):
    from open4d.codec import inspect_vmesh
    old = tmp_path / "capture.seq"
    old.write_bytes(_legacy_bytes())
    clip = bundle.Clip(name="capture", representation="gaussians",
                       frames=["capture/frame_0000.splat", "capture/frame_0001.splat"],
                       sequence={"url": "capture.seq", "frames": 2, "suffix": "splat", "bytes": old.stat().st_size})
    bundle.write(tmp_path, title="test", source="test", clips=[clip], fps=12)
    converted = sequence.pack_bundle(tmp_path)
    assert converted[0][1]["url"] == "capture.vmesh"
    assert bundle.read(tmp_path)["clips"][0]["sequence"]["url"] == "capture.vmesh"
    assert not old.exists()
    assert inspect_vmesh(tmp_path / "capture.vmesh")["sequence"]["frames"][1]["timestamp"] == 1 / 12
    assert sequence.pack_bundle(tmp_path) == []


def test_failed_bundle_migration_preserves_original_and_manifest(tmp_path, monkeypatch):
    old = tmp_path / "capture.seq"
    data = _legacy_bytes()
    old.write_bytes(data)
    clip = bundle.Clip(name="capture", representation="gaussians",
                       frames=["f0.splat", "f1.splat"],
                       sequence={"url": "capture.seq", "frames": 2})
    bundle.write(tmp_path, title="test", source="test", clips=[clip])
    original = (tmp_path / "view.json").read_bytes()
    monkeypatch.setattr(bundle, "write", lambda *args, **kwargs: (_ for _ in ()).throw(OSError("manifest write failed")))
    with pytest.raises(OSError, match="manifest write failed"):
        sequence.pack_bundle(tmp_path)
    assert old.read_bytes() == data
    assert (tmp_path / "view.json").read_bytes() == original


def test_public_open4d_load_supports_self_describing_browser_vmesh(tmp_path):
    import open4d
    import numpy as np
    from open4d.io._mesh import write_ply
    path = tmp_path / "frame.ply"
    positions = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32)
    triangles = np.array([[0, 1, 2]], dtype=np.uint32)
    write_ply(path, positions, triangles)
    target = tmp_path / "mesh.vmesh"
    sequence.pack([path], target, representation="mesh")
    with open4d.load(target) as restored:
        assert restored.timestamps == (0,)
        np.testing.assert_array_equal(restored[0].geometry.positions, positions)


def test_migration_keeps_a_legacy_container_referenced_by_an_unselected_clip(tmp_path):
    old = tmp_path / "shared.seq"
    old.write_bytes(_legacy_bytes())
    clips = [bundle.Clip(name=name, representation="gaussians", frames=["f0.splat", "f1.splat"],
                         sequence={"url": "shared.seq", "frames": 2}) for name in ("one", "two")]
    bundle.write(tmp_path, title="test", source="test", clips=clips)
    sequence.pack_bundle(tmp_path, names=["one"])
    assert old.is_file()
    index = bundle.read(tmp_path)
    assert index["clips"][0]["sequence"]["url"] == "shared.vmesh"
    assert index["clips"][1]["sequence"]["url"] == "shared.seq"


def test_legacy_bundle_migration_never_removes_paths_outside_the_bundle(tmp_path):
    root = tmp_path / "bundle"
    root.mkdir()
    sentinel = tmp_path / "keep.splat"
    sentinel.write_bytes(b"keep")
    old = root / "capture.seq"
    old.write_bytes(_legacy_bytes())
    clip = bundle.Clip(name="capture", representation="gaussians", frames=["../keep.splat", "f1.splat"],
                       sequence={"url": "capture.seq", "frames": 2})
    bundle.write(root, title="test", source="test", clips=[clip])
    with pytest.raises(ValueError, match="safe relative paths"):
        sequence.pack_bundle(root)
    assert sentinel.read_bytes() == b"keep"
    assert old.is_file()
    assert not (root / "capture.vmesh").exists()
