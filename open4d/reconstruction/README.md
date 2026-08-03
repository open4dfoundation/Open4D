# Reconstruction

Reconstruction components turn sensor captures into temporal geometry. The
`rgbd` component contains the existing synchronized multi-camera RGB-D fusion,
CUDA TSDF reconstruction, and streaming tools.

Reconstruction dependencies are component-local and are not required by the
lightweight `open4d` package.
