include_directories(${CMAKE_SOURCE_DIR}/include/reconstruction)

add_library(reconstruction
        reconstruction.cpp
        texture_mapping.cpp
)

if(MESHREDUCE_ENABLE_CUDA_KERNELS)
    target_sources(reconstruction PRIVATE texture_mapping_cuda.cu)
    target_compile_definitions(reconstruction
            PRIVATE MESHREDUCE_HAS_CUDA_TEXTURE_MAPPING=1)
    set_target_properties(reconstruction PROPERTIES
            CUDA_SEPARABLE_COMPILATION ON)
endif()

target_link_libraries(reconstruction
        Open3D::Open3D
        ${OpenCV_LIBS}
        jsoncpp
        Eigen3::Eigen
)

if(MESHREDUCE_ENABLE_CUDA_KERNELS)
    target_link_libraries(reconstruction CUDA::cudart)
endif()

if(MESHREDUCE_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(reconstruction OpenMP::OpenMP_CXX)
endif()
