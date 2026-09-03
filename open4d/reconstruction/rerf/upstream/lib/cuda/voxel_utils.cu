#include <torch/extension.h>

#include <cuda.h>
#include <cuda_runtime.h>

#include <vector>
#include <stdio.h>

#define CHECK_CPU(x) AT_ASSERTM(!x.is_cuda(), #x " must be a CPU tensor")
#define CHECK_CUDA(x) AT_ASSERTM(x.is_cuda(), #x " must be a CUDA tensor")
#define CHECK_CONTIGUOUS(x) AT_ASSERTM(x.is_contiguous(), #x " must be contiguous")
#define CHECK_FLOAT(x) AT_ASSERTM(x.scalar_type()==torch::ScalarType::Float, #x " must be a float tensor")
#define CHECK_INT(x) AT_ASSERTM(x.scalar_type()==torch::ScalarType::Int, #x " must be a Int tensor")
#define CHECK_SHORT(x) AT_ASSERTM(x.scalar_type()==torch::ScalarType::Short, #x " must be a Int tensor")
#define CHECK_LONG(x) AT_ASSERTM(x.scalar_type()==torch::ScalarType::Long, #x " must be a Long tensor")
#define CHECK_UCHAR(x) AT_ASSERTM(x.scalar_type()==torch::ScalarType::Byte, #x " must be a Int tensor")
#define CHECK_INPUT(x) CHECK_CUDA(x); CHECK_CONTIGUOUS(x);
#define CHECK_INPUT_CPU(x) CHECK_CPU(x); CHECK_CONTIGUOUS(x);



#define int64 int64_t

__global__
void merge_volume(float* in_tensor, int64 size_x, int64 size_y,  int64 size_z, 
                  int N, int feature_dim, int voxel_size,
                 float* out_tensor )    // (N, dim)
{
    int64 ids = blockDim.x * blockIdx.x + threadIdx.x; //  index of point

    int feature_id = ids % feature_dim;

    ids = ids / feature_dim;

    if (ids>=(size_x*voxel_size*size_y*voxel_size*size_z*voxel_size)) 
        return;

    int64 id_x = ids / (size_y*voxel_size*size_z*voxel_size);

    int64 tmp = ids - id_x* (size_y*voxel_size*size_z*voxel_size);

    int64 id_y =  tmp/(size_z*voxel_size);
    
    int64 id_z = tmp - id_y*(size_z*voxel_size);

    int64 voxel_coord_x = id_x%voxel_size;
    int64 voxel_coord_y = id_y%voxel_size;
    int64 voxel_coord_z = id_z%voxel_size;


    int64 voxel_id_x = id_x/voxel_size;
    int64 voxel_id_y = id_y/voxel_size;
    int64 voxel_id_z = id_z/voxel_size;

    int64 voxel_id = voxel_id_x*(size_y*size_z) + voxel_id_y*size_z + voxel_id_z;

    //if (voxel_id>=N) return;

    

    out_tensor[ids*feature_dim+feature_id] = in_tensor[voxel_id*voxel_size*voxel_size*voxel_size*feature_dim+ voxel_coord_x*voxel_size*voxel_size*feature_dim +voxel_coord_y*voxel_size*feature_dim
                                            + voxel_coord_z*feature_dim + feature_id ];
    
}


__global__
void merge_volume_backward(float* in_tensor, int64 size_x, int64 size_y,  int64 size_z, 
                  int N, int feature_dim, int voxel_size,
                 float* out_tensor )    // (N, dim)
{
    int64 ids = blockDim.x * blockIdx.x + threadIdx.x; //  index of point

    int feature_id = ids % feature_dim;

    ids = ids / feature_dim;

    if (ids>=(size_x*voxel_size*size_y*voxel_size*size_z*voxel_size)) 
        return;

    int64 id_x = ids / (size_y*voxel_size*size_z*voxel_size);

    int64 tmp = ids - id_x* (size_y*voxel_size*size_z*voxel_size);

    int64 id_y =  tmp/(size_z*voxel_size);
    
    int64 id_z = tmp - id_y*(size_z*voxel_size);

    int64 voxel_coord_x = id_x%voxel_size;
    int64 voxel_coord_y = id_y%voxel_size;
    int64 voxel_coord_z = id_z%voxel_size;


    int64 voxel_id_x = id_x/voxel_size;
    int64 voxel_id_y = id_y/voxel_size;
    int64 voxel_id_z = id_z/voxel_size;

    int64 voxel_id = voxel_id_x*(size_y*size_z) + voxel_id_y*size_z + voxel_id_z;

    //if (voxel_id>=N) return;



    in_tensor[voxel_id*voxel_size*voxel_size*voxel_size*feature_dim+ voxel_coord_x*voxel_size*voxel_size*feature_dim +voxel_coord_y*voxel_size*feature_dim
                                            + voxel_coord_z*feature_dim + feature_id  ] = out_tensor[ids*feature_dim+feature_id];

}

void merge_volume_forward(
    torch::Tensor in_tensor, //(N,voxel_size,voxel_size,voxel_size, dim)
    torch::Tensor in_size,  // (3,)
    torch::Tensor out_tensor // (size[1]*voxel_size,size[2]*voxel_size, size[3]*voxel_size, dim)
    )
{
    CHECK_INPUT(in_tensor); CHECK_FLOAT(in_tensor);
    CHECK_INPUT_CPU(in_size); CHECK_LONG(in_size);
    CHECK_INPUT(out_tensor); CHECK_FLOAT(out_tensor);

    AT_ASSERTM(in_tensor.size(4)== out_tensor.size(3), "feature dimension inconsistent");
    AT_ASSERTM(in_tensor.size(1)== in_tensor.size(2), "voxel size  inconsistent");
    AT_ASSERTM(in_tensor.size(2)== in_tensor.size(3), "voxel size  inconsistent");


    int64* p =in_size.data<int64>();

    int64 size_x = p[0];
    int64 size_y = p[1];
    int64 size_z = p[2];

    int N = in_tensor.size(0);
    int feature_dim = in_tensor.size(4);
    int voxel_size = in_tensor.size(1);


    AT_ASSERTM(in_tensor.size(0)== size_x*size_y*size_z, "out tensor size  inconsistent");

    AT_ASSERTM(out_tensor.size(0)== size_x*voxel_size, "out tensor size  inconsistent");
    AT_ASSERTM(out_tensor.size(1)== size_y*voxel_size, "out tensor size  inconsistent");
    AT_ASSERTM(out_tensor.size(2)== size_z*voxel_size, "out tensor size  inconsistent");

    dim3 dimBlock(256,1);
	dim3 dimGrid((size_x*voxel_size*size_y*voxel_size*size_z*voxel_size*feature_dim)/ dimBlock.x + 1, 1);


    merge_volume << <dimGrid, dimBlock >> > (
        (float*)in_tensor.data<float>(),
		size_x, size_y,size_z,
        N, feature_dim, voxel_size,
        (float*)out_tensor.data<float>());
}


void merge_volume_backward(
    torch::Tensor grad_tensor, //(size[1]*voxel_size,size[2]*voxel_size, size[3]*voxel_size, dim)
    torch::Tensor in_size,  // (3,)
    torch::Tensor out_grad_tensor  // (N,voxel_size,voxel_size,voxel_size, dim)
    )
{
    CHECK_INPUT(grad_tensor); CHECK_FLOAT(grad_tensor);
    CHECK_INPUT_CPU(in_size); CHECK_LONG(in_size);
    CHECK_INPUT(out_grad_tensor); CHECK_FLOAT(out_grad_tensor);


    AT_ASSERTM(out_grad_tensor.size(4)== grad_tensor.size(3), "feature dimension inconsistent");
    AT_ASSERTM(out_grad_tensor.size(1)== out_grad_tensor.size(2), "voxel size  inconsistent");
    AT_ASSERTM(out_grad_tensor.size(2)== out_grad_tensor.size(3), "voxel size  inconsistent");


    int64* p =in_size.data<int64>();

    int64 size_x = p[0];
    int64 size_y = p[1];
    int64 size_z = p[2];

    int N = out_grad_tensor.size(0);
    int feature_dim = out_grad_tensor.size(4);
    int voxel_size = out_grad_tensor.size(1);

    AT_ASSERTM(out_grad_tensor.size(0)== size_x*size_y*size_z, "out tensor size  inconsistent");

    AT_ASSERTM(grad_tensor.size(0)== size_x*voxel_size, "out tensor size  inconsistent");
    AT_ASSERTM(grad_tensor.size(1)== size_y*voxel_size, "out tensor size  inconsistent");
    AT_ASSERTM(grad_tensor.size(2)== size_z*voxel_size, "out tensor size  inconsistent");


    dim3 dimBlock(256,1);
	dim3 dimGrid((size_x*voxel_size*size_y*voxel_size*size_z*voxel_size*feature_dim)/ dimBlock.x + 1, 1);


    merge_volume_backward << <dimGrid, dimBlock >> > (
        (float*)out_grad_tensor.data<float>(),
		size_x, size_y,size_z,
        N, feature_dim, voxel_size,
        (float*)grad_tensor.data<float>());


}
