# Reconstruction

Reconstruction components turn sensor captures into temporal geometry.

- `rgbd` contains the synchronized multi-camera RGB-D fusion, CUDA TSDF
  reconstruction, and streaming tools.
- `gs_tools` contains Gaussian-splatting free-viewpoint video: QUEEN and
  3DGStream, pinned upstream, sharing one environment and one rasterizer. Each
  carries its own compression stage, which is why the pair lives here rather than
  under `codecs/`. **It is licensed for non-commercial use only**; see
  `gs_tools/THIRD_PARTY.md`.

Reconstruction dependencies are component-local and are not required by the
lightweight `open4d` package.
