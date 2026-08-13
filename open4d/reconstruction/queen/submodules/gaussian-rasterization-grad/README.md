# Gaussian Rasterization Grad

This folder modifies the rasterization engine provided by the paper "3D Gaussian Splatting for Real-Time Rendering of Radiance Fields". 

This folder contains a working version of our modified rasterizer. To reproduce the changes in this rasterizer, you can clone the original rasterizer (which we call `gaussian-rasterization-grad` in this folder for legacy reasons), and copy the modified files as follows:

```
git clone https://github.com/graphdeco-inria/diff-gaussian-rasterization gaussian-rasterization-grad
cd gaussian-rasterization-grad
git checkout 59f5f77e3ddbac3ed9db93ec2cfe99ed6c5d121d
```
and then copy the following files from this repository:
```
rasterize_points.cu
rasterize_points.h
setup.py
cuda_rasterizer/backward.h
cuda_rasterizer/forward.cu
cuda_rasterizer/rasterizer.h
cuda_rasterizer/forward.h
cuda_rasterizer/rasterizer_impl.h
cuda_rasterizer/auxiliary.h
cuda_rasterizer/backward.cu
cuda_rasterizer/rasterizer_impl.cu
```

If you can make use of it in your own research, please be so kind to cite the original work and ours.

<section class="section" id="BibTeX">
  <div class="container is-max-desktop content">
    <h2 class="title">BibTeX</h2>
    <pre><code>@Article{kerbl3Dgaussians,
      author       = {Kerbl, Bernhard and Kopanas, Georgios and Leimk{\"u}hler, Thomas and Drettakis, George},
      title        = {3D Gaussian Splatting for Real-Time Radiance Field Rendering},
      journal      = {ACM Transactions on Graphics},
      number       = {4},
      volume       = {42},
      month        = {July},
      year         = {2023},
      url          = {https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/}
}</code></pre>

<pre><code>@Article{girish2024queen,
  title={{QUEEN}: {QU}antized Efficient {EN}coding for Streaming Free-viewpoint Videos},
  author={Sharath Girish and Tianye Li and Amrita Mazumdar and Abhinav Shrivastava and David Luebke and Shalini De Mello},
  booktitle={The Thirty-eighth Annual Conference on Neural Information Processing Systems},
  year={2024},
  url={https://research.nvidia.com/labs/amri/projects/queen/}
}
  </div>
</section>