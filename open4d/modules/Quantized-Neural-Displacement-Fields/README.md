# Mesh Compression using Quantized Neural Displacement Fields

Step 1. Go to ssp_remesh folder and follow the instructions in it.

Step 2. Install Pytorch3D and its dependencies (https://github.com/facebookresearch/pytorch3d/blob/main/INSTALL.md)

Step 3. Install `dahuffman` and `tqdm` using `pip`

Step 4. Place any `.obj` file to be compressed in `objs_original/` folder. (Some meshes are already available)

Step 5. Run:

 ```python compress.py [mesh name] -ns [number of subdivisons] -cs [coarse mesh size] -hd [hidden dim size of INR] -nl [layers in INR]```

For Example:

 ```python compress.py pegasus -ns 3 -cs 7000 -hd 96 -nl 32```
