# Components

Each component has its own README and may add native tools, GPU extensions, or
hardware requirements to the shared Python baseline. See
[Requirements](requirements.md) for those additions.

## Mesh codecs

| Codec | |
|---|---|
| [**N4MC**](../open4d/codecs/n4mc/README.md) | Neural TSDF-based mesh compression, including a newer modular codec under its `data`, `models`, `losses`, `training`, and `evaluation` packages |
| [**QNDF**](../open4d/codecs/qndf/README.md) | Quantized Neural Displacement Fields: static mesh compression using an SSP coarse mesh and an implicit displacement decoder. [`qndf_int8`](../open4d/codecs/qndf_int8/README.md) is its quantized variant |
| [**TVMC**](../open4d/codecs/tvmc/README.md) | A Python, .NET, and Draco pipeline for tracked time-varying mesh compression, with setup and resumable pipeline scripts |
| [**TSMC**](../open4d/codecs/tsmc/README.md) | Scene-mesh compression with optional SAM-based static/dynamic separation, ARAP volume tracking, deformation, displacement compression, and evaluation |
| [**KLT**](../open4d/codecs/klt/README.md) | Karhunen–Loève Transform baseline that compresses TSDF voxel blocks with a learned linear basis and quantized coefficients, reconstructing meshes via marching cubes |
| [**Draco**](../open4d/codecs/draco/README.md) | Google Draco mesh-compression baseline. Wraps the vendored `draco_encoder`/`draco_decoder` binaries into a per-frame encode/decode/eval pipeline for benchmarking against the neural codecs |
| [**MPEG V-DMC test model**](../open4d/codecs/vdmc/README.md) | The pinned MPEG reference implementation for video-based dynamic mesh coding — reference encoder, decoder, metric tools, and unit tests. Separate from Open4D's TVMC research pipeline |
| [**Faster V-DMC**](../open4d/codecs/faster_vdmc/README.md) | A pinned performance-oriented fork of the same test model, with exact-output and higher-throughput modes recorded in the [benchmark report](benchmarks/faster-vdmc.md) |

## Reconstruction and streaming

| Module | |
|---|---|
| [**RGB-D**](../open4d/reconstruction/rgbd/README.md) | Synchronized multi-camera RGB-D ingestion, calibrated point-cloud fusion, CUDA TSDF mesh reconstruction, and live browser playback. Includes both the original native reconstruction code and the Python two-camera streaming pipeline |
| [**QUEEN**](../open4d/reconstruction/queen/README.md) | Quantized efficient encoding of dynamic Gaussians for streaming free-viewpoint video (NeurIPS 2024) |
| [**3DGStream**](../open4d/reconstruction/3dgstream/README.md) | On-the-fly training of 3D Gaussians for streaming photo-realistic free-viewpoint video (CVPR 2024) |
| [**Vega**](../open4d/reconstruction/vega/README.md) | An ORBIT adaptation of Vega (MobiCom 2025): mobile volumetric video streaming with 3D Gaussian splatting |
| [**ReRF**](../open4d/reconstruction/rerf/README.md) | Neural residual radiance fields (CVPR 2023) as a streamable compression method |
| [**gs-tools**](../open4d/reconstruction/gs_tools/README.md) | The one environment, rasterizers, and viewer the Gaussian methods share, plus `gs-tools view` for putting several methods under one camera |
| [**streamer**](../open4d/streamer/README.md) | Streaming and playback for 4D reconstructions, whatever their representation. Producers import it to describe and serve their output; it imports none of them |

## Integrations

| | |
|---|---|
| [**Open3D**](../integrations/open3d/README.md) | Converts decoded Open4D geometry into standard Open3D `TriangleMesh` or `PointCloud` objects. An adapter, not a loader |
| [**Unity**](../integrations/unity/README.md) | A Unity playback system for TVMC-encoded sequences: a C++ decoder backend and a C# front end, for XR targets |
