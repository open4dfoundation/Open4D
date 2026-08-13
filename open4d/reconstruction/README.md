# Reconstruction

Reconstruction components turn sensor captures into temporal geometry.

- `rgbd` contains the synchronized multi-camera RGB-D fusion, CUDA TSDF
  reconstruction, and streaming tools.
- `queen` and `3dgstream` are upstream copies of the two Gaussian-splatting
  free-viewpoint-video methods.
- `gs_tools` holds what the two share,
  and the single copies of `simple-knn` and `glm` they both compile against.
- Future Gaussian-splatting video methods should be included here.


Both ran to completion on the configuration below, from an environment built by
`gs_tools/environment.yml`. It is newer than what either upstream tested on, so
record your own alongside any number you intend to publish.

|              | Verified here            | QUEEN upstream | 3DGStream upstream |
| ------------ | ------------------------ | -------------- | ------------------ |
| OS           | Ubuntu 24.04.2 LTS       | Linux, unspecified | Ubuntu 22.04   |
| GPU          | RTX 4090, 24 GB, sm_89   | unspecified    | RTX A6000 / 3090   |
| Driver       | 595.84                   | —              | 535.86.05          |
| CUDA toolkit | 12.6 (nvcc V12.6.20)     | 11.8+          | 11.8               |
| GCC          | 13.3.0                   | —              | —                  |
| Python       | 3.12.13                  | 3.11           | 3.8                |
| PyTorch      | 2.7.0+cu126              | unpinned       | 2.0.1+cu118        |
| tiny-cuda-nn | 2.0                      | not used       | 1.7                |

The environment is 6.2 GB installed. Build the CUDA extensions on ext4: on an
ntfs3 mount ninja deadlocks part-way through.

|              | QUEEN                          | 3DGStream                        |
| ------------ | ------------------------------ | -------------------------------- |
| Scene        | coffee_martini, 300 frames x 17 cams | flame_steak, 19 timesteps  |
| Dataset      | 10 GB (N3DV)                   | 3.8 GB, plus the shipped init model and NTC checkpoint |
| Extra weights| MiDaS `dpt_beit_large_512.pt`, 1.5 GB | none                      |
| VRAM         | ~6 GB (sampled, not peak)      | ~1.5 GB (sampled, not peak)      |
| Wall time    | 24m41s                         | 1m54s                            |
| Result       | 26.24 dB test PSNR, 1.16 MB/frame | 34.89 dB test PSNR            |

