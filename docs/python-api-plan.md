# Python API plan for heterogeneous 3D sequences

Status: implementation in progress. Mesh loading and geometry-only file export
are implemented. Directory exports use `open4d.sequence.json` to preserve frame
identity, timing, metadata, and topology declarations. USD promotion, richer
manifest fields, additional representations, and plugins remain proposals.

## The central decision

Open4D should standardize the **Python view of a sequence**, not require one
canonical way to save it.

A numbered OBJ folder, a time-sampled USD file, a reconstructed frame store,
and a codec decoder have different ownership, timing, seeking, and dependency
semantics. Making one of those layouts universal would either discard useful
information or make simple data unnecessarily difficult to use. They can,
however, all provide the same finite, lazy `Sequence` interface.

```text
path / container / manifest / decoder
                 |
          format-specific reader
                 |
            FrameProvider
                 |
              Sequence
                 |
      metrics / codecs / viewers / export
```

The existing `TriangleMesh -> Frame -> FrameProvider -> Sequence` layering is
the correct base for this API. Storage detection and decoding belong above the
core, in `open4d.io`; file-format details should not leak into `Sequence`.

## User-facing API

The common path should remain one call:

```python
from open4d.io import open_sequence

with open_sequence("capture/", fps=30.0) as sequence:
    print(len(sequence), sequence.duration, sequence.topology)
    mesh = sequence[0].geometry
```

The proposed first public surface is:

```python
def open_sequence(
    source: str | os.PathLike[str],
    *,
    format: str | None = None,
    fps: float | None = None,
    options: Mapping[str, object] | None = None,
) -> Sequence: ...

def inspect_sequence(
    source: str | os.PathLike[str],
    *,
    format: str | None = None,
) -> SequenceInfo: ...

def write_sequence(
    sequence: Sequence,
    destination: str | os.PathLike[str],
    *,
    format: str | None = None,
    overwrite: bool = False,
    options: Mapping[str, object] | None = None,
) -> Path: ...

def available_formats() -> tuple[FormatInfo, ...]: ...
```

Design details:

- `format=None` performs deterministic detection. A value such as `"usd"` or
  `"obj"` bypasses detection and is the escape hatch for ambiguous
  sources.
- `fps` supplies timing only when a source has no timestamps. It must not
  silently replace timing declared by a container or manifest. Explicit
  retiming should be a separate sequence operation.
- `options` carries uncommon reader-specific settings, such as a USD prim path,
  without growing the common function every time a backend adds a feature.
  Readers must reject unknown options rather than ignore misspellings.
- The implemented `inspect_sequence` reports the selected format, storage,
  frame count, timing source, geometry kind, and topology declaration without
  decoding geometry. Richer dependency and capability reporting remains
  planned.
- `write_sequence` is a conversion boundary, not a promise that every format
  can represent every field. It must report unsupported or lossy fields before
  writing unless the caller explicitly enables a documented lossy policy.

The initial implementation should accept local paths only. URLs, object stores,
file-like objects, and live sources introduce lifecycle, range-request, and
authentication contracts that deserve separate designs.

## Reader and writer boundary

Readers adapt storage into providers. A small internal protocol keeps format
selection separate from decoding:

```python
class SequenceReader(Protocol):
    format_id: str

    def probe(self, source: Source) -> ProbeResult: ...
    def inspect(self, source: Source) -> SequenceInfo: ...
    def open(self, source: Source, options: OpenOptions) -> FrameProvider: ...


class SequenceWriter(Protocol):
    format_id: str

    def inspect_write(
        self, sequence: Sequence, destination: Path, options: WriteOptions
    ) -> WritePlan: ...

    def write(
        self, sequence: Sequence, destination: Path, options: WriteOptions
    ) -> Path: ...
```

`ProbeResult` should include a confidence and a reason, not only a boolean.
Detection then follows a visible and testable order:

1. an explicit `format` argument;
2. an Open4D sequence manifest in a directory;
3. a format signature or container schema marker;
4. a unique supported file suffix or unambiguous directory layout;
5. otherwise, `AmbiguousFormatError` or `UnsupportedFormatError`.

The public loader should never silently choose the most common suffix in a
mixed directory. That behavior is convenient for a demo but can omit frames in
a research run without failing. A mixed directory needs an explicit format or
manifest.

The reader registry should initially be internal and contain built-in readers.
After two external adapters exist, it can become an explicit `IORegistry`
object and optionally support Python entry points. Deferring global plugin
registration avoids import-order behavior before the extension contract is
known.

## Storage forms supported by the same model

The API should distinguish a storage form from a geometry representation:

| Storage form | Example | Reader behavior |
| --- | --- | --- |
| Single static file | `frame.ply` | One-frame sequence with timestamp `0` |
| Implicit frame directory | `frame_0001.obj`, ... | Lazy per-file reads; caller supplies missing timing |
| Manifested frame directory | manifest plus heterogeneous referenced files | Manifest owns ordering, timing, metadata, and declarations |
| Sequence container | time-sampled `.usd` / `.usdc` | Container reader owns timing and resource cleanup |
| Codec artifact | Draco frames, TVMC bundle, neural model | Decoder-backed provider produces frames lazily |
| In-memory sequence | existing `Frame` objects | `MemoryFrameProvider`; no I/O detection |

OpenUSD remains the preferred rich offline interchange target in the current
roadmap, but it is one writer/reader pair rather than the definition of a
sequence. Lightweight users should still be able to load OBJ and PLY folders
with NumPy alone.

### Directory manifests

Implicit folders are useful but cannot reliably express timestamps, missing
frames, coordinate systems, units, topology, or mixed payload formats. Open4D
should therefore define a small, versioned JSON manifest for directory-based
sequences. A sketch:

```json
{
  "schema": "open4d.sequence-manifest/v1",
  "geometry_kind": "triangle_mesh",
  "coordinate_system": {"up_axis": "Y", "unit_meters": 1.0},
  "topology": "changing",
  "frames": [
    {"index": 41, "timestamp": 1.3667, "uri": "mesh_0041.ply"},
    {"index": 43, "timestamp": 1.4333, "uri": "mesh_0043.ply"}
  ],
  "metadata": {"capture_id": "example"}
}
```

The frame list, not filename parsing, becomes authoritative when a manifest is
present. Paths must resolve relative to the manifest, remain within the source
root by default, and be validated before frame access. The schema should permit
gaps in source frame indices while keeping ordinal `Sequence` indexing dense.

This manifest describes existing resources; it is not itself a codec or a
packed container. It also gives codec and reconstruction adapters a low-cost
way to expose results before a richer native container exists.

## Information the common model must preserve

Every reader should preserve, or explicitly report that it cannot provide:

- dense ordinal access and the original source frame index;
- finite timestamps and their clock/timebase meaning;
- geometry kind and attributes;
- topology, vertex-count, and correspondence declarations;
- coordinate system, unit scale, and transforms;
- sequence and frame metadata;
- source identity sufficient for actionable errors;
- resource lifecycle and lazy-decoding behavior.

Metadata values in supported interchange should be JSON-compatible and
namespaced for producer-specific fields. Large arrays, model weights, codec
payloads, and arbitrary Python objects do not belong in metadata.

The current core has no coordinate-system or transform contract. P1 can carry
these as validated sequence declarations, but round-trip support should not be
claimed until concrete types and tests exist.

## Geometry diversity

The first public `open4d.io` release remains mesh-valued. For compatibility with
the existing core and readers, a zero-face source temporarily appears as a
`TriangleMesh` with zero triangles. Consumers must treat that convention as a
point-cloud placeholder, not as the eventual representation contract. TSDFs
and Gaussians must not be forced into mesh attributes.

Representation support should expand from real adapter requirements:

1. stabilize mesh I/O with the existing `TriangleMesh` and `Frame`;
2. add a concrete `PointCloud` value and allow `Frame.geometry` to be the
   supported geometry union;
3. add transforms and units;
4. define volume/TSDF and Gaussian values only with their owning pipelines.

At that point the typing can become `Frame[GeometryT]` and
`Sequence[GeometryT]` for static analysis while retaining the same runtime
provider boundary. Concrete value types are preferable to a wide class
hierarchy: consumers can declare exactly which geometry kinds they support and
fail before decoding a long sequence.

Mixed geometry kinds within one sequence should be rejected in v1. If a future
use case genuinely needs them, it should introduce an explicit scene/sample
model rather than weakening every consumer's assumptions.

## Failures are part of the API

Public I/O should expose an `Open4DError` hierarchy with at least:

- `SourceNotFoundError`;
- `UnsupportedFormatError`;
- `AmbiguousFormatError`;
- `MissingDependencyError` with an install-extra hint;
- `InvalidSequenceError` for structural or schema failures;
- `DecodeError` naming the source and frame;
- `UnsupportedFeatureError` for valid data the selected reader cannot retain;
- `LossyWriteError` when export would discard information.

Library code should raise these exceptions, never `SystemExit` or print a
warning. CLI examples may translate them into concise terminal messages.

## Proposed package layout

```text
open4d/io/
  __init__.py          # small public surface
  _api.py              # open/inspect/write orchestration
  _errors.py
  _formats.py          # FormatInfo, registry, probing
  _manifest.py         # directory manifest v1
  _options.py
  readers/
    mesh_directory.py  # built-in OBJ/PLY and single-frame paths
    usd.py              # lazy optional dependency
  writers/
    mesh_directory.py
    usd.py
```

The base installation remains NumPy-only. OpenUSD, Trimesh, Open3D, Torch, and
codec dependencies must be imported only inside the backend that needs them.
The explicit setuptools package allowlist and distribution-content tests must
be updated alongside the new package.

## Implementation slices

### Slice 1: public mesh loading

- Move the built-in OBJ/PLY and folder providers out of
  `examples/visualization` into `open4d.io`.
- Add `open_sequence`, `inspect_sequence`, format records, and typed errors.
- Preserve lazy construction and context-manager cleanup.
- Make the examples thin clients of the public API.
- Test static files, numbered directories, missing timing, mixed-directory
  ambiguity, malformed frames, and optional-dependency messages.

### Slice 2: USD round trip

- Move the existing USD reader/writer behind the public registry.
- Ratify the USD schema described in the roadmap.
- Round-trip every supported mesh field, source index, timestamp, topology
  declaration, and sequence metadata.
- Add preflight reporting for unsupported/lossy fields.

### Slice 3: manifested directories

- Specify and validate `open4d.sequence-manifest/v1`.
- Support explicit ordering, timestamp gaps, source indices, relative paths,
  units, coordinate declarations, and per-frame metadata.
- Reuse the same tiny redistributable fixture in loading, USD, metrics, and
  later codec tests.

### Slice 4: real provider adapters

- Adapt the Draco vertical slice, then TVMC decoded output and RGB-D finite
  replay.
- Require each adapter to declare topology and correspondence honestly.
- Use evidence from those adapters before freezing a public plugin API.

### Slice 5: representation expansion

- Introduce `PointCloud` based on captured/reconstructed data.
- Generalize frame/sequence type annotations without breaking mesh users.
- Design volume and Gaussian values separately when their pipelines are ready.

## Acceptance criteria for the first release

- The documented one-call example works for a single OBJ/PLY file and a frame
  directory in the NumPy-only installation.
- Opening and inspection do not decode a frame unless the storage format makes
  that unavoidable and reports it.
- Format selection is deterministic and its result is inspectable.
- Declared source timing is never silently replaced.
- Mixed or ambiguous folders fail rather than omit data.
- All decoded arrays pass through the existing canonical dtype validation.
- Missing optional dependencies produce an exact install command.
- Readers identify the failing file and frame in errors.
- Examples import the public API rather than owning a second loader registry.
- The wheel-content and Python 3.10–3.13 test gates continue to pass.

## Slice 1 verification and benchmark

`scripts/benchmark_io.py` calls the Python API directly; neither the API nor
the harness invokes a subprocess. Fixture creation and parser warm-up happen
before timing, and every measured decode asserts the resulting vertex and
triangle shapes. The test tier additionally covers exporter-style OBJ records,
an independently encoded binary PLY, a transformed/colorized GLB, a big-endian
PLY fallback, and lazy random access over 1,000 numbered frame files.

On 2026-08-19, the default benchmark used 120 binary PLY frames with 4,096
vertices and 7,938 triangles per frame (five repetitions, reported median):

| Environment | Discover | PLY decode | Full iteration | Traced peak, one PLY |
| --- | ---: | ---: | ---: | ---: |
| macOS arm64, Python 3.13 (source) | 0.74 ms | 5.11 ms | 210 frames/s | 1.62 MB |
| Ubuntu x86_64, Python 3.12 (editable) | 0.54 ms | 13.94 ms | 90.6 frames/s | 1.62 MB |
| Ubuntu x86_64, Python 3.12 (clean wheel) | 0.54 ms | 10.20 ms | 97.1 frames/s | 1.62 MB |

With `[tools]` installed on the Ubuntu host, the same mesh decoded in 6.70 ms
from OFF, 1.51 ms from STL, 1.46 ms from GLB, and 1.63 ms from glTF. STL is
validated by triangle count and index bounds because the format does not retain
shared-vertex indexing.

These are reproducibility baselines, not CI thresholds: shared-host timing is
too variable for a stable pass/fail gate. CI instead runs a smaller benchmark
that keeps its correctness assertions and exercises every advertised format
available in the `[tools]` environment.

## Modular codec and visualization follow-up

`open4d.codec.Codec` is the minimal replaceable encode/decode boundary. Five
lossless in-process reference implementations (`raw`, `deflate`, `bzip2`,
`lzma`, and byte `rle`) share a safe NumPy-array container; `npz` remains the
default DEFLATE alias. Research codecs can implement the same protocol once
their decoder state is self-contained. `open4d.visualization.visualize` and
`render_gif` now own the former example Qt renderer and import all GUI
dependencies lazily.

The executable notebook at `examples/open4d_sequence_codec.ipynb` opens all 157
frames in `4d_files/Rafa_Approves_hd_4k` lazily and defaults to a two-frame
demonstration. It attempts every codec registered in that environment, reports
missing optional/native prerequisites, validates every successful fresh decode,
and opens each successful result in the public visualizer outside headless mode.
The headless test requires all six NumPy-container identifiers to succeed and
asserts that no registered codec silently disappears from the result table.

On the same macOS/Python 3.13 machine, ten materialized Rafa frames (199,960
vertices and 400,000 triangles total) produced artifacts from 3.19 MB (LZMA) to
11.94 MB (RLE). Encode time ranged from 0.002 s (raw) to 1.90 s (LZMA), while
validated full decode ranged from 0.015 s (raw) to 0.25 s (LZMA). The benchmark
checks every geometry channel, custom attribute, timestamp, and metadata value;
wall-clock timing runs separately from memory tracing to avoid instrumentation
bias. These reference-codec measurements are in-process, and source parsing is
reported separately. V-DMC is a separate native-process backend.

## Questions to settle with the first implementation

1. Should the manifest filename be fixed (for example,
   `open4d.sequence.json`) or discovered by schema content?
2. Which coordinate-system declarations are required in v1, and which may be
   unknown rather than guessed?
3. Should `write_sequence` initially expose only USD, or also a manifested
   OBJ/PLY directory writer for a dependency-free round trip?
4. Does `inspect_sequence` guarantee zero geometry decode, or report an
   `inspection_cost` capability for formats that require it?
5. Which format-specific settings have proved common enough to promote out of
   `options` into typed arguments after the first two readers?

These questions can be answered with the small OBJ/PLY and USD slices. They do
not need to block the central architecture: storage remains replaceable,
providers remain lazy, and `Sequence` remains the Python contract.
