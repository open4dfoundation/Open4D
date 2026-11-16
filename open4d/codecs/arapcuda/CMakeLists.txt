cmake_minimum_required(VERSION 3.24)
# must be set before project(), which otherwise fills in the compiler default
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
  set(CMAKE_CUDA_ARCHITECTURES 75 80 86 89 90)
endif()
project(arapcuda VERSION 0.1.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_core python/bindings.cpp
  src/voxelize.cu src/nearest.cu src/tracker.cu)
target_include_directories(_core PRIVATE src)
# IEEE float/double without FMA contraction on device and host, so the
# voxelization is bit-identical to the C# reference.
target_compile_options(_core PRIVATE
  $<$<COMPILE_LANGUAGE:CUDA>:-fmad=false -prec-div=true -prec-sqrt=true -ftz=false -Xcompiler=-ffp-contract=off>
  $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)
# static CUDA runtime: users need only the driver
set_target_properties(_core PROPERTIES CUDA_RUNTIME_LIBRARY Static)
install(TARGETS _core LIBRARY DESTINATION arapcuda)
