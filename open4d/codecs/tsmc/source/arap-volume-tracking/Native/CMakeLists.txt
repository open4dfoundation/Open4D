cmake_minimum_required(VERSION 3.24)
# must be set before project(), which otherwise fills in the compiler default
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
  set(CMAKE_CUDA_ARCHITECTURES 89)   # RTX 4090
endif()
project(arapcuda LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(arapcuda SHARED src/voxelize.cu src/nearest.cu)
target_include_directories(arapcuda PUBLIC include)

# Bit-exact agreement with the C# reference needs IEEE float/double without
# FMA contraction, on both the device and the host side.
target_compile_options(arapcuda PRIVATE
  $<$<COMPILE_LANGUAGE:CUDA>:-fmad=false -prec-div=true -prec-sqrt=true -ftz=false -Xcompiler=-ffp-contract=off>
  $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)

# Put the library next to Client.dll so DllImport("arapcuda") finds it.
set_target_properties(arapcuda PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
  CUDA_RUNTIME_LIBRARY Static)
