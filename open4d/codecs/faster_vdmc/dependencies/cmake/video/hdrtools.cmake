if ( USE_HDRTOOLS )
  add_compile_definitions(USE_HDRTOOLS)
  set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/hdrtools )
  if( NOT EXISTS ${DIR} )
    CPMAddPackage( NAME             hdrtools
                  GIT_REPOSITORY    http://gitlab.com/standards/HDRTools.git
                  GIT_TAG           v0.23
                  SOURCE_DIR        ${DIR}
                  DOWNLOAD_ONLY     YES )
  endif()
  add_subdirectory(${DIR}/common)
endif()

