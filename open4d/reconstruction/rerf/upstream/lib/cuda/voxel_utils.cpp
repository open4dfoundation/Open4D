#include <torch/extension.h>
#include <vector>






void merge_volume_forward(
    torch::Tensor in_tensor, //(N,voxel_size,voxel_size,voxel_size, dim)
    torch::Tensor in_size,  // (3,)
    torch::Tensor out_tensor // ( size[1]*voxel_size,size[2]*voxel_size, size[3]*voxel_size, dim)
    );


void merge_volume_backward(
    torch::Tensor grad_tensor, //(dim, size[1]*voxel_size,size[2]*voxel_size, size[3]*voxel_size)
    torch::Tensor in_size,  // (3,)
    torch::Tensor out_grad_tensor  // (N,dim,voxel_size,voxel_size,voxel_size)
    );



PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  m.def("merge_volume_forward", &merge_volume_forward, "merge_volume_for forward (CUDA)");
  m.def("merge_volume_backward", &merge_volume_backward, "merge_volume backward (CUDA)");
}