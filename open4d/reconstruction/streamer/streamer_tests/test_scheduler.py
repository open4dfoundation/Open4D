"""The client's scheduler, and the one rule it shares with `open4d.core`.

`chain` exists twice -- as `open4d.core.Dependency.chain` in Python and as a
transcription in `viewer.html`, because the client cannot run Python. Two
implementations of one rule is a liability, so the first half of this file holds
them to each other over the same cases rather than trusting that they agree.

The second half tests what the scheduler does with the plan: caching, eviction,
look-ahead, and the decoder position that makes a forward seek cheap and a
backward one in a non-rewindable stream expensive.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import textwrap
from itertools import product
from pathlib import Path

import pytest
from open4d.core import Dependency, DependencyMode

from streamer import bundle
from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")


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
        if prefix.startswith("const"):
            end = page.index(";\n", start) + 2
        else:
            end = page.index("\n}\n", start) + 3
        chunks.append(page[start:end])
    return "\n".join(chunks)


def run_js(body: str, tmp_path: Path, name: str = "s.mjs") -> object:
    """Run ``body`` with the page's scheduling code in scope; return its JSON."""
    script = tmp_path / name
    script.write_text(
        _extract("INDEPENDENT", "dependencyOf", "chain", "Scheduler")
        + "\n"
        + textwrap.dedent(body)
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    if finished.returncode:
        raise AssertionError(finished.stderr)
    return json.loads(finished.stdout)


# ------------------------------------------------- one rule, two languages ---

CASES = [
    (Dependency(), 0),
    (Dependency(), 7),
    (Dependency(mode=DependencyMode.GOP, key_frames=(0,)), 17),
    (Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8)), 6),
    (Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8)), 9),
    (Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8)), 4),
    (Dependency(mode=DependencyMode.SEQUENTIAL), 0),
    (Dependency(mode=DependencyMode.SEQUENTIAL), 5),
]
POSITIONS = [None, 0, 3, 4, 5, 9, 17]


@requires_node
def test_the_js_chain_matches_the_python_one_exactly(tmp_path):
    """The drift guard. Change one implementation and this fails."""
    plan = []
    expected = []
    for (dependency, index), decoded in product(CASES, POSITIONS):
        field = bundle.dependency_field(dependency) or {
            "mode": "independent",
            "key_frames": [],
        }
        plan.append({"dependency": field, "index": index, "decoded": decoded})
        expected.append(list(dependency.chain(index, decoded=decoded)))

    body = f"""
        const plan = {json.dumps(plan)};
        const out = plan.map((c) => chain(
            dependencyOf({{dependency: c.dependency}}), c.index, c.decoded));
        process.stdout.write(JSON.stringify(out));
    """
    assert run_js(body, tmp_path) == expected


@requires_node
def test_an_absent_declaration_is_independent_on_both_sides(tmp_path):
    assert bundle.dependency_of({"name": "c"}).mode is DependencyMode.INDEPENDENT
    body = """
        process.stdout.write(JSON.stringify([
          dependencyOf({}).mode,
          dependencyOf({dependency: null}).mode,
          dependencyOf({dependency: {mode: "gop", key_frames: [0]}}).mode,
        ]));
    """
    assert run_js(body, tmp_path) == ["independent", "independent", "gop"]


@requires_node
def test_a_sequential_backward_seek_is_a_replay_in_js_too(tmp_path):
    """The asymmetry the whole model exists for: ReRF's stream does not rewind."""
    body = """
        const seq = {mode: "sequential", keyFrames: []};
        process.stdout.write(JSON.stringify({
          forward: chain(seq, 5, 3),
          backward: chain(seq, 2, 9),
        }));
    """
    result = run_js(body, tmp_path)
    assert result["forward"] == [4, 5]
    assert result["backward"] == [0, 1, 2]
    assert list(Dependency(mode=DependencyMode.SEQUENTIAL).chain(2, decoded=9)) == [
        0, 1, 2
    ]


# ---------------------------------------------------------- the scheduler ---

# A decode that records what it was asked for, so a test can assert on the
# order and count of decodes rather than only on the value returned.
HARNESS = """
    globalThis.fetched = [];
    globalThis.fetch = (url) => {
      globalThis.fetched.push(url);
      return Promise.resolve({
        ok: true,
        arrayBuffer: () => Promise.resolve(new ArrayBuffer(16)),
      });
    };
    const clipOf = (frames, dependency) => ({
      frames: Array.from({length: frames}, (_, i) => `f${i}.bin`),
      dependency,
    });
    const decode = (buffer, url) => ({url});
"""


@requires_node
def test_an_independent_clip_fetches_only_the_frame_asked_for(tmp_path):
    body = HARNESS + """
        const s = new Scheduler(clipOf(10, null), "", decode, {cacheSize: 4});
        (async () => {
          await s.seek(7);
          process.stdout.write(JSON.stringify({
            fetched: globalThis.fetched, snap: s.snapshot(),
          }));
        })();
    """
    result = run_js(body, tmp_path)
    assert result["fetched"] == ["f7.bin"]
    assert result["snap"]["decodes"] == 1
    assert result["snap"]["mode"] == "independent"


@requires_node
def test_a_gop_clip_decodes_from_its_key_frame(tmp_path):
    body = HARNESS + """
        const dep = {mode: "gop", key_frames: [0, 4, 8]};
        const s = new Scheduler(clipOf(12, dep), "", decode, {cacheSize: 8});
        (async () => {
          await s.seek(6);
          process.stdout.write(JSON.stringify(globalThis.fetched));
        })();
    """
    assert run_js(body, tmp_path) == ["f4.bin", "f5.bin", "f6.bin"]


@requires_node
def test_a_forward_step_reuses_the_decoder_position(tmp_path):
    body = HARNESS + """
        const dep = {mode: "gop", key_frames: [0, 4, 8]};
        const s = new Scheduler(clipOf(12, dep), "", decode, {cacheSize: 2});
        (async () => {
          await s.seek(6);
          globalThis.fetched = [];
          await s.seek(7);
          process.stdout.write(JSON.stringify(globalThis.fetched));
        })();
    """
    assert run_js(body, tmp_path) == ["f7.bin"]


@requires_node
def test_the_cache_evicts_oldest_first(tmp_path):
    """The image path used to keep every frame it had ever shown."""
    body = HARNESS + """
        const s = new Scheduler(clipOf(10, null), "", decode, {cacheSize: 3});
        (async () => {
          for (const i of [0, 1, 2, 3]) await s.seek(i);
          process.stdout.write(JSON.stringify({
            cached: s.snapshot().cached, keys: [...s.cache.keys()],
          }));
        })();
    """
    result = run_js(body, tmp_path)
    assert result["cached"] == 3
    assert result["keys"] == [1, 2, 3]


@requires_node
def test_a_cache_hit_costs_no_fetch(tmp_path):
    body = HARNESS + """
        const s = new Scheduler(clipOf(5, null), "", decode, {cacheSize: 4});
        (async () => {
          await s.seek(2);
          globalThis.fetched = [];
          await s.seek(2);
          process.stdout.write(JSON.stringify({
            fetched: globalThis.fetched, snap: s.snapshot(),
          }));
        })();
    """
    result = run_js(body, tmp_path)
    assert result["fetched"] == []
    assert result["snap"]["hits"] == 1
    assert result["snap"]["decodes"] == 1


@requires_node
def test_a_repeat_seek_keeps_the_frame_from_being_evicted(tmp_path):
    """Insertion-ordered Map as an LRU only works if a hit re-inserts."""
    body = HARNESS + """
        const s = new Scheduler(clipOf(10, null), "", decode, {cacheSize: 2});
        (async () => {
          await s.seek(0);
          await s.seek(1);
          await s.seek(0);       // 0 becomes the newest
          await s.seek(2);       // so 1 is evicted, not 0
          process.stdout.write(JSON.stringify([...s.cache.keys()]));
        })();
    """
    assert run_js(body, tmp_path) == [0, 2]


@requires_node
def test_prefetch_looks_ahead_and_counts_bytes(tmp_path):
    body = HARNESS + """
        const s = new Scheduler(clipOf(10, null), "", decode,
                                {cacheSize: 8, lookAhead: 3});
        (async () => {
          await s.seek(1);
          s.prefetch(1);
          await new Promise((r) => setTimeout(r, 30));
          process.stdout.write(JSON.stringify({
            fetched: globalThis.fetched.sort(), bytes: s.snapshot().bytes,
          }));
        })();
    """
    result = run_js(body, tmp_path)
    assert result["fetched"] == ["f1.bin", "f2.bin", "f3.bin", "f4.bin"]
    assert result["bytes"] == 4 * 16


@requires_node
def test_prefetch_does_not_restart_a_dependent_stream_at_the_loop_point(tmp_path):
    """Wrapping to frame 0 would evict the frames being played to get there."""
    body = HARNESS + """
        const dep = {mode: "sequential", key_frames: []};
        const s = new Scheduler(clipOf(4, dep), "", decode,
                                {cacheSize: 8, lookAhead: 3});
        (async () => {
          await s.seek(3);
          globalThis.fetched = [];
          s.prefetch(3);          // 0, 1, 2 are all across the wrap
          await new Promise((r) => setTimeout(r, 30));
          process.stdout.write(JSON.stringify(globalThis.fetched));
        })();
    """
    assert run_js(body, tmp_path) == []


@requires_node
def test_an_http_error_rejects_and_leaves_nothing_pending(tmp_path):
    body = """
        globalThis.fetch = () => Promise.resolve({ok: false, status: 404});
        const clip = {frames: ["f0.bin"], dependency: null};
        const s = new Scheduler(clip, "", () => ({}), {});
        (async () => {
          let message = null;
          try { await s.seek(0); } catch (error) { message = error.message; }
          process.stdout.write(JSON.stringify({
            message, pending: s.snapshot().pending,
          }));
        })();
    """
    result = run_js(body, tmp_path)
    assert "404" in result["message"]
    assert result["pending"] == 0


# ------------------------------------------------------- decoded-frame life ---

# Stubs for the browser bits a pixel frame touches. `Image` resolves on the next
# tick, like a real decode, and every createObjectURL/revokeObjectURL is
# recorded so a test can assert on the lifetime rather than the tidiness.
IMAGE_HARNESS = """
    globalThis.created = [];
    globalThis.revoked = [];
    globalThis.Blob = class { constructor(parts, options) { this.options = options; } };
    globalThis.URL = {
      createObjectURL: () => {
        const url = `blob:${globalThis.created.length}`;
        globalThis.created.push(url);
        return url;
      },
      revokeObjectURL: (url) => { globalThis.revoked.push(url); },
    };
    globalThis.Image = class {
      set src(value) {
        this._src = value;
        setTimeout(() => this.onload && this.onload(), 0);
      }
      get src() { return this._src; }
    };
    globalThis.fetch = (url) => Promise.resolve({
      ok: true,
      headers: { get: () => "image/jpeg" },
      arrayBuffer: () => Promise.resolve(new ArrayBuffer(8)),
    });
    const clipOf = (n) => ({
      frames: Array.from({length: n}, (_, i) => `f${i}.jpg`),
      dependency: null,
    });
"""


@requires_node
def test_a_decoded_frames_src_is_still_usable_when_a_pane_assigns_it(tmp_path):
    """The regression this exists for.

    A pane shows a pixel frame by copying the decoded frame's `src` onto its own
    <img>. Revoking the object URL as soon as the decode finished left that copy
    pointing at a dead blob -- and assigning a dead blob URL is not an error, so
    every pixel pane went silently blank while the Gaussian one kept working.
    """
    body = IMAGE_HARNESS + """
        const s = new Scheduler(clipOf(4), "", decodeImage, {cacheSize: 4});
        (async () => {
          const frame = await s.seek(0);
          process.stdout.write(JSON.stringify({
            src: frame.src,
            revokedYet: globalThis.revoked,
            usable: !globalThis.revoked.includes(frame.src),
          }));
        })();
    """
    result = run_js(
        _extract("decodeImage") + body, tmp_path, name="life.mjs"
    )
    assert result["revokedYet"] == []
    assert result["usable"] is True
    assert result["src"].startswith("blob:")


@requires_node
def test_eviction_releases_the_blob(tmp_path):
    """The other half: held for as long as it is cached, and no longer."""
    body = IMAGE_HARNESS + """
        const s = new Scheduler(clipOf(6), "", decodeImage, {cacheSize: 2});
        (async () => {
          for (const i of [0, 1, 2]) await s.seek(i);
          process.stdout.write(JSON.stringify({
            created: globalThis.created.length,
            revoked: globalThis.revoked,
            cached: s.snapshot().cached,
          }));
        })();
    """
    result = run_js(_extract("decodeImage") + body, tmp_path, name="evict.mjs")
    assert result["created"] == 3
    assert result["revoked"] == ["blob:0"]      # only the evicted one
    assert result["cached"] == 2


@requires_node
def test_release_frees_everything_a_scheduler_still_holds(tmp_path):
    """A pane changing clip replaces its scheduler; the old one must let go."""
    body = IMAGE_HARNESS + """
        const s = new Scheduler(clipOf(4), "", decodeImage, {cacheSize: 4});
        (async () => {
          for (const i of [0, 1, 2]) await s.seek(i);
          s.release();
          process.stdout.write(JSON.stringify({
            revoked: globalThis.revoked.sort(),
            cached: s.snapshot().cached,
          }));
        })();
    """
    result = run_js(_extract("decodeImage") + body, tmp_path, name="release.mjs")
    assert result["revoked"] == ["blob:0", "blob:1", "blob:2"]
    assert result["cached"] == 0


@requires_node
def test_a_geometry_frame_has_nothing_to_release(tmp_path):
    """release must be safe on frames that hold no browser resource."""
    body = """
        globalThis.URL = { revokeObjectURL: () => { throw new Error("called"); } };
        Scheduler.release(undefined);
        Scheduler.release(null);
        Scheduler.release({count: 3, positions: []});
        process.stdout.write(JSON.stringify("ok"));
    """
    assert run_js(body, tmp_path, name="norelease.mjs") == "ok"


# ---------------------------------------------------------- the wire format ---


def test_independent_is_omitted_rather_than_written():
    assert bundle.dependency_field(Dependency()) is None
    assert bundle.dependency_field(None) is None


def test_a_declared_dependency_round_trips(tmp_path):
    declared = Dependency(mode=DependencyMode.GOP, key_frames=(0, 8))
    clip = bundle.Clip(
        name="c",
        representation="gaussians",
        frames=["c/f0.ply"],
        dependency=bundle.dependency_field(declared),
    )
    bundle.write(tmp_path, title="t", source="s", clips=[clip])
    stored = bundle.read(tmp_path)["clips"][0]
    assert bundle.dependency_of(stored) == declared


def test_key_frames_are_dropped_for_a_mode_that_cannot_use_them():
    """SEQUENTIAL has no key frames; a manifest claiming otherwise is ignored."""
    recovered = bundle.dependency_of(
        {"dependency": {"mode": "sequential", "key_frames": [0, 4]}}
    )
    assert recovered.mode is DependencyMode.SEQUENTIAL
    assert recovered.key_frames == ()


def test_every_exporter_still_writes_independent_frames(tmp_path):
    """Stated as a fact rather than assumed: they all decode before writing."""
    import numpy as np
    from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh

    from streamer import export

    geometry = TriangleMesh(
        np.asarray([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
        np.asarray([[0, 1, 2]], dtype=np.uint32),
    )
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0.0, geometry)]))
    clip = export.from_sequence(sequence, tmp_path, name="c")
    assert clip.dependency is None
    assert bundle.dependency_of(clip).mode is DependencyMode.INDEPENDENT


# ------------------------------------------ letting go of the last subject ---
# Explore holds a whole clip resident: for one subject that is ~119 MB (a 56 MB
# point cloud and a 63 MB Gaussian container, both decoded). Nine subjects
# browsed in a row is a gigabyte if a switch does not free the last one.


@requires_node
def test_releasing_a_scheduler_frees_every_frame_it_held(tmp_path):
    body = HARNESS + """
        const s = new Scheduler(clipOf(8, null), "", decode, {cacheSize: 8});
        (async () => {
          for (let i = 0; i < 8; i++) await s.seek(i);
          const before = s.snapshot().cached;
          s.release();
          process.stdout.write(JSON.stringify({
            before, after: s.snapshot().cached,
            pending: s.snapshot().pending, abandoned: s.abandoned,
            resident: s.resident,
          }));
        })();
    """
    result = run_js(body, tmp_path)
    assert result["before"] == 8
    # Nothing held, and marked so a download still in flight stops rather than
    # decoding into a cache nobody will read.
    assert result["after"] == 0
    assert result["pending"] == 0
    assert result["abandoned"] is True
    assert result["resident"] is False


@requires_node
def test_a_released_scheduler_stops_decoding_mid_download(tmp_path):
    """Switching subject during a download must not keep filling the cache.

    Without the `abandoned` check the loop runs to the end of a 63 MB
    container, decoding every frame into a scheduler whose pane is gone.
    """
    body = HARNESS + """
        let served = 0;
        globalThis.fetch = (url) => {
          served += 1;
          return Promise.resolve({
            ok: true,
            arrayBuffer: () => Promise.resolve(new ArrayBuffer(16)),
          });
        };
        const s = new Scheduler(clipOf(30, null), "", decode, {});
        (async () => {
          const download = s.downloadAll(null);
          s.release();                    // the pane moved on
          const held = await download;
          process.stdout.write(JSON.stringify({
            held, cached: s.snapshot().cached, served,
          }));
        })();
    """
    result = run_js(body, tmp_path)
    assert result["held"] is False, "a released scheduler must not report success"
    assert result["cached"] == 0
    # It stopped early rather than fetching all thirty.
    assert result["served"] < 30


@requires_node
def test_a_released_scheduler_is_not_downloaded_again(tmp_path):
    """`downloadSelection` takes its pane list before the first await, so by the
    time it reaches a pane that pane may belong to another subject."""
    body = HARNESS + """
        const s = new Scheduler(clipOf(4, null), "", decode, {});
        s.release();
        process.stdout.write(JSON.stringify({abandoned: s.abandoned}));
    """
    result = run_js(body, tmp_path)
    assert result["abandoned"] is True

    page = viewer_path().read_text()
    start = page.index("async function downloadSelection(")
    loop = page[start:page.index("\n}\n", start)]
    # The guard that reads it, so a disposed pane is skipped rather than
    # reported as a decode failure onto DOM that has left the page.
    assert "!pane.source || pane.source.abandoned" in loop


@requires_node
def test_a_late_image_decode_does_not_strand_its_blob_url(tmp_path):
    """The same race, for the resource a browser will not reclaim on its own.

    `release` revokes the object URL of every cached frame and then clears the
    cache. A decode still in flight resolves after that, so its frame is never
    in the cache the revoke pass walked -- and an image's blob stays alive for
    the lifetime of the page. Browsing nine subjects strands one per in-flight
    frame.
    """
    body = """
        globalThis.revoked = [];
        globalThis.URL = {
          createObjectURL: () => "blob:x",
          revokeObjectURL: (url) => { globalThis.revoked.push(url); },
        };
        const s = new Scheduler(
          {frames: ["f0.bin"], dependency: null}, "", () => {}, {});
        s.release();
        // What `_decodeOne` does when its fetch lands late.
        s._touch(0, {objectUrl: "blob:late"});
        process.stdout.write(JSON.stringify({
          cached: s.snapshot().cached, revoked: globalThis.revoked,
        }));
    """
    result = run_js(body, tmp_path)
    assert result["cached"] == 0
    # Released rather than dropped on the floor.
    assert result["revoked"] == ["blob:late"]
