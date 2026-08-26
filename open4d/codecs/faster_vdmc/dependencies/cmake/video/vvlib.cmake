if ( USE_VV_VIDEO_CODEC )
  add_compile_definitions(USE_VV_VIDEO_CODEC)
  # Load threads
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)

  # Update GCC 
  message("CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")    
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 9 )
      add_compile_options(-Wno-error=deprecated-copy)
    endif ()
    add_compile_options(-Wno-error=implicit-fallthrough
                        -Wno-error=type-limits
                        -Wno-error=shift-negative-value)
  endif()

  # VVENC
  set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/vvenc )
  if( NOT EXISTS ${DIR} )
    CPMAddPackage( NAME             vvenc
                  GIT_REPOSITORY    https://github.com/fraunhoferhhi/vvenc.git 
                  GIT_TAG           v1.7.0
                  SOURCE_DIR        ${DIR}
                  DOWNLOAD_ONLY     YES )
    # execute_process( COMMAND sed -i "s/{CMAKE_SOURCE_DIR}/{CMAKE_CURRENT_SOURCE_DIR}/g" ${DIR}/CMakeLists.txt )          
  endif()
  if( EXISTS ${DIR}/CMakeLists.txt )
      if( NOT EXISTS ${DIR}/PATCHED )  
        file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/patches/vvenc/*")
        foreach(file ${files})
          message("git am ${file}")
          execute_process( COMMAND git am ${file} WORKING_DIRECTORY ${DIR} RESULT_VARIABLE ret )
          if(NOT ${ret} EQUAL "0")
            message(FATAL_ERROR "Error during the draco patch process. ")
          endif()
        endforeach()
        file(WRITE ${DIR}/PATCHED "patched")
      endif()
    endif()
  add_subdirectory(${DIR})

  # VVDEC
  set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/vvdec )
  if( NOT EXISTS ${DIR} )
    CPMAddPackage( NAME             vvdec
                  GIT_REPOSITORY    https://github.com/fraunhoferhhi/vvdec.git 
                  GIT_TAG           v1.6.0
                  SOURCE_DIR        ${DIR}
                  DOWNLOAD_ONLY     YES )
  endif()
  add_compile_options(-DTARGET_SIMD_X86)
  add_subdirectory(${DIR})
endif()

