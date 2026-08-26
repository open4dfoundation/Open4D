cmake_minimum_required(VERSION 3.17)
project(vmesh CXX C )

# Options:
set(VDMC_VERSION_MAJOR 14 )
set(VDMC_VERSION_MINOR 0 )

# Video encoder and decoder:
OPTION( USE_HM_VIDEO_CODEC        "Use HM video encoder and decoder"                      TRUE  ) 
OPTION( USE_X265_VIDEO_CODEC      "Use x265 video encoder (GPL; opt-in)"                  FALSE )
OPTION( USE_VTM_VIDEO_CODEC       "Use VTM video encoder and decoder"                     FALSE  ) 
OPTION( USE_VV_VIDEO_CODEC        "Use VVENC and VVDEC video encoder and decoder"         FALSE  ) 
OPTION( USE_SHMAPP_VIDEO_CODEC    "Use SHM video encoder and decoder" 		 			  FALSE  )

# Geometry encoder and decoder:       
OPTION( USE_DRACO_GEOMETRY_CODEC  "Use draco geometry encoder and decoder"                FALSE  )

# Color conversion library:       
OPTION( USE_HDRTOOLS              "Use HDRTools for color conversions"                    FALSE )

# Use HM optimized RDO        
OPTION( USE_HM_PCC_RDO            "Use HM optimized RDO "                                 TRUE )

# Code texture and displacement video in one picture
OPTION( USE_JOINT_CODING          "Use joint coding "                                     TRUE )

# Test applications       
OPTION( BUILD_WRAPPER_APPS        "Build wrapper applications"                            TRUE  )
OPTION( BUILD_UNIT_TEST_APPS      "Build unit test applications"                          FALSE )

# Set CODE_CODEC_ID to enable/disable codecIds saving in vmesh bitstream        
# Note: Disable to stay compliant with v1.0       
OPTION( CODE_CODEC_ID             "Code codec id in bitstreams"                           FALSE )
OPTION( BITSTREAM_TRACE           "Enable bitstream logs"                                 TRUE )
OPTION( CONFORMANCE_LOG           "Enable conformance logs"                               FALSE )
OPTION( PREPROCESSING_VERBOSE     "Logout details for preprocessing"                      FALSE )

# Mesh coordinate type: float or double
SET( MESH_TYPE "double" CACHE STRING "Mesh coordinate type: float or double" )

# Building options


enable_language(C)
if (MSVC)
  add_definitions("/W3 /D_CRT_SECURE_NO_WARNINGS /nologo")
else ()
  add_compile_options( -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-deprecated-declarations)   
endif()
if (UNIX)
  if(NOT APPLE)
    add_definitions(-D_POSIX_C_SOURCE=200809L)
  endif()  
elseif (WIN32)
  add_definitions(-D_USE_MATH_DEFINES)
endif()

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_XM_NO_INTRINSICS_")
# compile flags still to be harmonized to guarantee strict same compilation results on float operations
if (NOT WIN32)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -m64 -DNDEBUG -mfpmath=sse -ffp-contract=off -stdlib=libc++ \
        -ffp-model=strict ") # -Wno-unknown-warning-option
  else()
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -m64 -DNDEBUG -mfpmath=sse -ffp-contract=off -ffloat-store")
  endif()
endif()

# Set mesh types
if( ${MESH_TYPE} STREQUAL "double" OR ${MESH_TYPE} STREQUAL "float" )  
  add_compile_definitions(MeshType=${MESH_TYPE})
else()
  message( FATAL_ERROR "MESH_TYPE not correctly defined (${MESH_TYPE})")
endif()

# Set codec codec ID
if( ${CODE_CODEC_ID} )  
  add_compile_definitions(CODE_CODEC_ID)
endif()
if( ${BITSTREAM_TRACE} )  
  add_compile_definitions(BITSTREAM_TRACE)
endif()
if( ${CONFORMANCE_LOG} )  
  add_compile_definitions(CONFORMANCE_LOG)
endif()
if( ${FAST_MEB_ENCODE} )  
  add_compile_definitions(FAST_MEB_ENCODE)
endif()
if( ${USE_JOINT_CODING} )
  add_compile_definitions(USE_JOINT_CODING)
endif()
# Tools
include( dependencies/cmake/tools/CPM.cmake )
include( dependencies/cmake/tools/tools.cmake )
include( dependencies/cmake/tools/clang-format.cmake )
include( dependencies/cmake/tools/cppcheck.cmake )

# Dependencies
include( dependencies/cmake/geometry/directx.cmake )
include( dependencies/cmake/geometry/uvatlas.cmake )
include( dependencies/cmake/geometry/mmetric.cmake )
include( dependencies/cmake/geometry/tinyply.cmake )

# Wrapper libraries
include( dependencies/cmake/video/hdrtools.cmake )
include( dependencies/cmake/video/hm.cmake )
include( dependencies/cmake/video/x265.cmake )
include( dependencies/cmake/video/vtm.cmake )
include( dependencies/cmake/video/vvlib.cmake )
include( dependencies/cmake/geometry/draco.cmake )
include( dependencies/cmake/video/shm_app.cmake )

# vmesh core codec
add_all_subdirectory(source/wrapper)
add_all_subdirectory(source/lib)

# Applications
add_subdirectory(source/app)
