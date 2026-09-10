cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(MeshReduce VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local/open3d-0.18" "/usr/local")

option(MESHREDUCE_ENABLE_CUDA_KERNELS
       "Use custom CUDA kernels for texture-camera selection and UV generation"
       ON)
option(MESHREDUCE_ENABLE_OPENMP
       "Parallelize the CPU texture-mapping fallback with OpenMP"
       ON)

find_package(jsoncpp REQUIRED)
find_package(Open3D 0.18.0 REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(Eigen3 3.4 REQUIRED)
find_package(OpenCV REQUIRED
        core imgproc imgcodecs highgui calib3d)
find_package(k4a REQUIRED)
# The Femto Bolt K4A compatibility package exports k4arecord with this
# dependency but does not install an OrbbecSDK CMake config file.
if(NOT TARGET OrbbecSDK::OrbbecSDK)
    find_library(ORBBECSDK_LIBRARY OrbbecSDK REQUIRED)
    add_library(OrbbecSDK::OrbbecSDK SHARED IMPORTED)
    set_target_properties(OrbbecSDK::OrbbecSDK PROPERTIES
            IMPORTED_LOCATION "${ORBBECSDK_LIBRARY}")
endif()
find_package(k4arecord REQUIRED)
find_package(draco CONFIG REQUIRED)

if(MESHREDUCE_ENABLE_CUDA_KERNELS)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24
           AND NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
            set(CMAKE_CUDA_ARCHITECTURES native)
        endif()
    else()
        message(WARNING
                "No CUDA compiler found; custom texture kernels are disabled")
        set(MESHREDUCE_ENABLE_CUDA_KERNELS OFF)
    endif()
endif()

if(MESHREDUCE_ENABLE_OPENMP)
    find_package(OpenMP)
    if(NOT OpenMP_CXX_FOUND)
        message(WARNING
                "OpenMP was not found; the texture-mapping CPU fallback is serial")
    endif()
endif()

include_directories(include)

add_subdirectory(src)

add_subdirectory(app)

include(CTest)
if(BUILD_TESTING AND MESHREDUCE_ENABLE_CUDA_KERNELS)
    add_executable(texture_mapping_cuda_test
            tests/texture_mapping_cuda_test.cpp
            src/reconstruction/texture_mapping_cuda.cu)
    target_include_directories(texture_mapping_cuda_test
            PRIVATE include/reconstruction)
    target_link_libraries(texture_mapping_cuda_test PRIVATE CUDA::cudart)
    set_target_properties(texture_mapping_cuda_test PROPERTIES
            CUDA_SEPARABLE_COMPILATION ON)
    add_test(NAME texture_mapping_cuda_test
             COMMAND texture_mapping_cuda_test)
endif()

link_libraries(sensor reconstruction jsoncpp streaming)
