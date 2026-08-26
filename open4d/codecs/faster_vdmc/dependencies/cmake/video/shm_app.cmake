if ( USE_SHMAPP_VIDEO_CODEC )
  add_compile_definitions(USE_SHMAPP_VIDEO_CODEC)
  cmake_minimum_required(VERSION 3.17 FATAL_ERROR)
  
  SET( SHM_VERSION         SHM-12.4 )
  SET( SHM_DIR             ${CMAKE_SOURCE_DIR}/dependencies/${SHM_VERSION}/ )
  set( SHM_APP_SOURCE_DIR  ${SHM_DIR}/source )
  MESSAGE("Clone SHM application: ${SHM_DIR}") 
  
  IF( NOT EXISTS "${SHM_DIR}/COPYING" )
    MESSAGE("  - SHM application clone")
    EXECUTE_PROCESS( COMMAND git clone --depth 1 https://vcgit.hhi.fraunhofer.de/jvet/SHM.git --branch ${SHM_VERSION} ${SHM_DIR} )
  ENDIF()
  
  if( NOT EXISTS "${SHM_DIR}/PATCHED" )
      set( SHM_PATCH ${CMAKE_SOURCE_DIR}/dependencies/patches/shm/SHM12.4_with_joint_coding.patch )
    message("Apply patch: ${SHM_PATCH}")
    execute_process( COMMAND git apply ${SHM_PATCH} --whitespace=nowarn WORKING_DIRECTORY ${SHM_DIR} RESULT_VARIABLE ret )
    if( NOT ${ret} EQUAL "0")
      message( FATAL_ERROR "Error during the SHM patch process.")
    endif()
    file( WRITE ${SHM_DIR}/PATCHED "SHM patched with: " ${SHM_PATCH} )   
  endif()
  
  add_custom_target(SHM ALL
    COMMAND make -C ${CMAKE_SOURCE_DIR}/dependencies/SHM-12.4/build/linux all
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/SHM-12.4/build/linux
    COMMENT "Building SHM"
  )
endif()
