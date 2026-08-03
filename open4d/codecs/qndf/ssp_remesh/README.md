To build this module, perform the following steps in this folder:
```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j8
```
If everything goes well, you should be able to find and run the executable with
```
./ssp_remesh_bin [mesh_path] [target_faces] [number_subdivision] [random_seed]
```

This module for **Remeshing using Successive Self-Parameterization** is a minimal and slighltly modifiled version of the implementation of the [Surface Multigrid via Intrinsic Prolongation](https://github.com/HTDerekLiu/surface_multigrid_code/tree/main/08_subdiv_remesh).