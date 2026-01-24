cmake_minimum_required(VERSION 3.16)
project(Backend_Decoder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------- ANDROID BUILD SETTINGS -------------------
if(ANDROID)
  message(STATUS "Android build: enabling exceptions & RTTI")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti -fPIC")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1")  # Or -O0 if debugging Android

  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
      ${CMAKE_SOURCE_DIR}/build_android)
endif()

# ------------------- DEPENDENCIES -------------------
add_subdirectory(external/eigen)

# ------------------- TVMDecoder LIBRARY -------------------
add_library(TVMDecoder SHARED
  src/core/TVMDecoder.cpp
  src/core/TVMDecoder.h
  src/core/TVMDecoder_Extern.cpp
  src/core/PlaybackManager.cpp
  src/core/PlaybackManager.h
  src/core/TVMUtil.cpp
  src/core/TVMUtil.h

  src/io/MatrixIO.cpp
  src/io/MatrixIO.h
  src/io/SimpleMeshIO.cpp
  src/io/SimpleMeshIO.h

  src/mesh/SimpleMesh.cpp
  src/mesh/SimpleMesh.h

  src/logger/TVMLogger.cpp
  src/logger/TVMLogger.h
)

add_compile_options(-fexceptions)

target_include_directories(TVMDecoder PRIVATE
  ${CMAKE_SOURCE_DIR}/external/eigen
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/io
  ${CMAKE_SOURCE_DIR}/src/mesh
  ${CMAKE_SOURCE_DIR}/src/logger
)

# Link platform-specific libraries
if(ANDROID)
  target_link_libraries(TVMDecoder PRIVATE
    log
    z
  )
else()
  target_link_libraries(TVMDecoder PRIVATE
    z
  )
endif()

# ------------------- EIGEN SETTINGS -------------------
target_compile_definitions(TVMDecoder PRIVATE
  EIGEN_DONT_VECTORIZE
  EIGEN_DISABLE_UNALIGNED_ARRAY_ASSERT
)

target_compile_options(TVMDecoder PRIVATE
  -fno-fast-math
  -ffp-contract=off
  -fno-associative-math
  -fno-math-errno
)

