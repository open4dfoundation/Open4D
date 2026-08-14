# Glossary

Use this page while reading code, papers, and the roadmap. Definitions describe
how the term is used in Open4D, not every possible meaning in graphics.

| Term | Meaning |
| --- | --- |
| 3D | Three spatial coordinates, usually x/y/z. |
| 4D | In this project, 3D geometry changing over time. |
| AABB | Axis-aligned bounding box: minimum/maximum coordinates along each axis. Its diagonal is the current PSNR peak convention. |
| ACK | Acknowledgement message. OBP1 ACKs identify the accepted pair and carry their own CRC. |
| Adapter | Conversion at a boundary, such as Open4D geometry to Open3D, without taking ownership of storage or timing. |
| ARAP | As-rigid-as-possible deformation, a way to deform geometry while locally resisting non-rigid distortion. |
| Artifact | A file/bundle needed to reproduce or decode a run: bitstream, model, reference mesh, basis, transform, metadata, etc. |
| Attribute | Numeric/boolean data aligned with mesh vertices, faces, or face corners. |
| Basis | A set of directions/functions used to represent data; KLT stores coefficients in a learned linear basis. |
| Bitrate | Bits per unit time. State whether this is codec payload, container, or wire bitrate. |
| Calibration | Camera intrinsics/extrinsics and cross-camera transforms that map pixels/depth into consistent coordinates. |
| C2C / point-to-point | Distance from each point/vertex to the nearest point/vertex in another set. |
| C2P / point-to-plane | Nearest offset projected onto a reference normal; motion along the tangent plane is discounted. |
| Chamfer distance | Usually a reduction of nearest-neighbor distances in both directions between point sets. Exact conventions vary. |
| Clock domain | The clock that produced a timestamp, such as camera device, sender wall clock, receiver monotonic clock, or presentation timeline. |
| Codec | Encoder/decoder rules that turn a representation into an artifact and back. Not a container or network protocol. |
| COLMAP | Structure-from-motion/multi-view-stereo tooling and file formats used to estimate camera poses and sparse scene points. |
| Container | A package for streams and metadata, such as an OpenUSD sequence file. |
| Coordinate frame/system | Origin, axis directions/handedness, up axis, and units used by coordinates. |
| Correspondence | A promise that an element (especially vertex index `i`) represents the same surface location across frames. |
| CRC | Cyclic redundancy check used to detect accidental corruption; it is not authentication or encryption. |
| Decoder state | Every value needed to decode: weights, reference/coarse mesh, basis/mean, normalization, topology, tables, dependencies, etc. |
| Delta | An update that depends on a prior/base state rather than a self-contained frame. |
| Depth image | Image whose pixels store distance from a camera rather than color. |
| D1/D2 | MPEG-style geometry metrics commonly referring to point-to-point and point-to-plane variants; always name the implementation/convention. |
| DRC | Common filename extension for a Draco-compressed geometry payload. |
| Entropy coding | Lossless packing that gives likely symbols shorter representations. |
| Epoch | A live-session generation used with session/frame IDs to distinguish reconnects or restarts. |
| Extent | Geometry bounds, stored per time sample in the example USD container. |
| Extrinsics | Transform from one camera/world coordinate frame to another. |
| Face | A polygonal surface element. Open4D core stores triangles only. |
| FPS | Frames per second. For irregular timestamps, Open4D's finite sequence reports average FPS. |
| Frame | Geometry plus stored frame index, timestamp, and metadata. |
| FrameProvider | Minimal finite storage/decoding contract: frame count plus random access, with optional declarations/cleanup. |
| Free-viewpoint video (FVV) | A dynamic scene representation that can render viewpoints different from the captured cameras. |
| Fresh-process decode | Starting a new decoder process with only declared artifacts; used to reveal hidden encoder/source dependencies. |
| Gaussian splat / 3DGS | An oriented translucent 3D Gaussian with appearance parameters, rendered by projection/blending; not a triangle mesh. |
| Goodput | Useful application data delivered per unit time, excluding protocol overhead and unusable/retransmitted data. |
| GPU rasterizer | Specialized GPU code that turns geometry/Gaussians into image pixels. |
| Handedness | Orientation convention relating x/y/z axes. |
| Hausdorff distance | Maximum nearest distance between sets in both directions. Current example “Hausdorff” uses sampled vertices, not continuous surfaces. |
| ICP | Iterative closest point alignment. Shared metrics do not run ICP automatically. |
| Intrinsics | Camera focal length/principal point/distortion parameters mapping pixels and rays. |
| Keyframe | Self-contained frame/base from which dependent deltas can be decoded. In example USD, also a topology-change sample. |
| KLT | Karhunen–Loève Transform, equivalent to PCA under common conditions; a data-learned linear transform. |
| Latency | Time between two named events on reconciled clocks. Always name endpoints, such as capture-to-present. |
| Latent | Compact learned feature tensor consumed by a neural decoder. |
| Laziness | Deferring decode/work until a frame or property actually needs it. |
| LiveSource | Proposed unknown-length stream abstraction with buffering, time, dependency, closure, and failure semantics. It is not `Sequence`. |
| Manifest | Structured record of revision, input, configuration, environment, artifacts/hashes/bytes, timing, and results. |
| Marching cubes | Algorithm that extracts triangles approximating an isosurface, commonly the TSDF zero level. |
| Material | Shading/appearance description referencing colors, textures, and parameters; not yet in Open4D core. |
| Mesh | Vertices plus connectivity (and optional attributes) describing a surface. |
| Metadata | Context not represented by the primary value fields. It should be documented, small, and not a hidden codec artifact. |
| MRD1/MRD2/MRD3 | Reconstruction mesh/texture transport experiments: one-shot raw, one-shot Draco, and continuous Draco/JPEG respectively. |
| Nearest neighbor | Closest sample in another set under a distance measure. |
| Neural representation | A learned model/latent whose evaluation reconstructs a signal or geometry field. |
| Normal | Direction perpendicular to a surface, used by lighting and point-to-plane metrics. |
| NTC | Neural transformation cache used by 3DGStream to update Gaussians between frames. |
| OBP1 | Versioned bounded protocol for a synchronized pair of compressed RGB-D camera observations. |
| Open3D | External 3D processing/visualization library used by research and integration code. |
| OpenUSD / USD | Scene/interchange framework with time sampling. Selected for Open4D offline interchange; not a codec. |
| Ordinal | Position in a sequence. It may differ from the stored source `frame_index`. |
| Payload bytes | Bytes produced by a codec or carried as message content, excluding any separately stated container/network overhead. |
| PLY | Polygon File Format; used for per-frame mesh/point-cloud files. |
| Point cloud | Unconnected 3D samples, often from depth cameras. Core lacks a first-class point-cloud type in v0.2-dev. |
| Presentation timestamp | When a sample should be displayed on a declared playback timeline. |
| PSNR | Peak signal-to-noise ratio in decibels. Requires both error convention and peak definition. Higher is better; exact match can be infinite. |
| Quantization | Mapping values to fewer levels, introducing controlled loss and reducing representation size. |
| Random access | Ability to request a particular finite frame without consuming all earlier frames. |
| Reconstruction | Inferring 3D geometry/representation from observations such as RGB-D or multi-view images. |
| Reference mesh | Base mesh against which temporal deformation/displacements are stored. |
| Registration | Transforming data into a common coordinate frame. Shared metrics assume it is already done. |
| Renderer | Produces images from a representation, camera, lighting, and appearance. |
| RMS error | Square root of mean squared error; aggregation details determine whether frames/vertices have equal weight. |
| SAM3 | Segmentation model vendored as a TSMC submodule for static/dynamic scene separation. |
| Self-contained artifact | Bundle from which a fresh decoder reconstructs output without undeclared originals or encoder memory. |
| Sequence | Open4D's finite lazy random-access temporal geometry abstraction. |
| SequenceView | Lazy slice mapping into another sequence. |
| Side information | Values besides primary coded symbols that decoding requires; they count toward a complete artifact. |
| SDF | Signed distance field, whose zero level represents a surface. |
| SSP | QNDF's surface simplification/subdivision/projection preprocessing path. |
| SSIM | Structural Similarity Index, an image-quality metric. It is not a mesh-distance metric. |
| StreamSample | Proposed live value carrying geometry/payload plus session, identity, time, dependencies, and statistics. |
| Submodule | Git repository pinned inside another repository. An uninitialized submodule has a recorded commit but no checked-out contents. |
| Texture | Image mapped to a surface, typically through UV coordinates. |
| Timestamp | Numeric time value meaningful only with units and clock/timeline semantics. |
| Topology | Mesh connectivity. Fixed topology means triangle indices remain invariant. |
| TopologyMode | Open4D declaration: `FIXED`, `CHANGING`, or `UNKNOWN`. |
| Transform | Mapping between coordinate frames, commonly a 4x4 homogeneous matrix. |
| Triangle | Face connecting three vertex indices. |
| TSDF | Truncated signed distance field; a bounded-band volumetric surface representation used for fusion/compression. |
| UV | Two-dimensional texture coordinate, per vertex or face corner. |
| V-DMC | MPEG video-based dynamic mesh coding test model/reference implementation. |
| Vertex | Indexed 3D position used by mesh faces. |
| Vertex-set metric | Metric evaluated on stored/sampled vertices rather than all points of continuous faces. |
| Volume | 3D grid/field representation. Open4D core has no volume type in v0.2-dev. |
| Voxel | One cell/sample of a 3D grid. |
| WebRTC | Real-time communication stack; Open3D uses it here to serve the live browser view. |
| Wire bytes | All bytes transmitted, including message/protocol overhead at the specified layer. |
| Winding | Order of face vertices, normally controlling front direction and normal orientation. |
