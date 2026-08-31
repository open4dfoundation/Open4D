# Why this baseline trains its own ReRF sequences

NeVo streams ReRF content, so the simulator needs ReRF feature-voxel
sequences as input. Upstream's dataset (`kpop`, `box`, `sing`, plus a
pre-compressed `kpop`) is **not** downloadable: per
[ReRF_Dataset](https://github.com/aoliao12138/ReRF_Dataset), access requires a
signed licence form returned to ShanghaiTech, students are explicitly not
eligible to request it, and the Google Drive is permissioned.

So this baseline makes its own, from the ORBIT mesh sequences this repo
already carries. That is the same substitution the NeVo paper made for two of
its six datasets: "We render the 8i and V-SENSE datasets' high-quality point
clouds to images from different viewports and use them to train NeRF videos"
(section 5.1). `orbitnevo/prepare.py` rasterises ORBIT's textured OBJ
sequences into the NHR-format corpus `rerf/lib/load_NHR.py` reads, and
`orbitnevo/train.py` trains ReRF over it.

If a licensed copy of the real ReRF dataset ever lands here, it drops in
without touching the simulator: run upstream's `data_util.py` over it to get
`cams_<frame>.json`, add a `bbox.json`, and point `--corpus` at the result.
