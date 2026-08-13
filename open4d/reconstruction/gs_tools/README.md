# gs-tools

- `environment.yml` — the one environment both are built and run in.
- `simple-knn/` — one copy, QUEEN's, whose added `<float.h>`/`<cfloat>` includes
  are what let it compile under GCC 13. Both trees import it as `simple_knn._C`,
  by module name, so moving it here changed nothing in either.
- `glm/` — one copy. All three rasterizers vendored byte-identical trees, and it
  is header-only, so sharing it is only an `-I` path.

## Setup

    conda env create -f environment.yml
    conda activate open4d-gs

Then build the five CUDA extensions from `open4d/reconstruction/`, all with
`--no-build-isolation` (an isolated build has no torch to compile against):

    export TCNN_CUDA_ARCHITECTURES=89      # sm_89 = RTX 4090; set to your card
    pip install --no-build-isolation \
      git+https://github.com/NVlabs/tiny-cuda-nn/#subdirectory=bindings/torch
    pip install --no-build-isolation gs_tools/simple-knn
    pip install --no-build-isolation queen/submodules/diff-gaussian-rasterization
    pip install --no-build-isolation queen/submodules/gaussian-rasterization-grad
    pip install --no-build-isolation 3dgstream/submodules/diff-gaussian-rasterization

Build on ext4. On an ntfs3 mount ninja deadlocks in `ntfs_file_write_iter`.


