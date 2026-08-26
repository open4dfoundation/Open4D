if(USE_X265_VIDEO_CODEC)
  add_compile_definitions(USE_X265_VIDEO_CODEC)

  set(X265_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/x265)
  if(NOT EXISTS ${X265_DIR}/source/CMakeLists.txt)
    CPMAddPackage(
      NAME x265
      GIT_REPOSITORY https://bitbucket.org/multicoreware/x265_git.git
      GIT_TAG b81f650e21e8aacbe6a9ad04ce14aefc05b932c0
      SOURCE_DIR ${X265_DIR}
      DOWNLOAD_ONLY YES)
  endif()

  set(HIGH_BIT_DEPTH ON CACHE BOOL "Build x265 Main10" FORCE)
  set(MAIN12 OFF CACHE BOOL "Build x265 Main12" FORCE)
  set(ENABLE_SHARED OFF CACHE BOOL "Build shared x265" FORCE)
  set(ENABLE_CLI OFF CACHE BOOL "Build x265 CLI" FORCE)
  set(ENABLE_HDR10_PLUS OFF CACHE BOOL "Build HDR10+ support" FORCE)
  set(EXPORT_C_API ON CACHE BOOL "Build x265 C API" FORCE)
  add_subdirectory(${X265_DIR}/source ${CMAKE_BINARY_DIR}/x265 EXCLUDE_FROM_ALL)
endif()
