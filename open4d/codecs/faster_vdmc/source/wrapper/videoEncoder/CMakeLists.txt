cmake_minimum_required(VERSION 3.2)

get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project(${MYNAME} C CXX)

file(GLOB SRC  *.h 
               *.cpp  
               ${CMAKE_SOURCE_DIR}/dependencies/program-options-lite/program_options_lite.cpp
               ${CMAKE_SOURCE_DIR}/dependencies/program-options-lite/program_options_lite.h )

include_directories( . 
                     ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                     ${CMAKE_SOURCE_DIR}/dependencies/program-options-lite )

set( LIBS vmeshCommon )

# HM
if( USE_HM_VIDEO_CODEC )
  include_directories( ${HM_LIB_SOURCE_DIR} )
  set( LIBS ${LIBS} TLibCommon TLibEncoder )
endif()

# x265 Main10
if(USE_X265_VIDEO_CODEC)
  include_directories(${X265_DIR}/source)
  include_directories(${CMAKE_BINARY_DIR}/x265)
  find_library(NUMA_LIBRARY numa)
  set(LIBS ${LIBS} x265-static)
  if(NUMA_LIBRARY)
    set(LIBS ${LIBS} ${NUMA_LIBRARY})
  endif()
endif()

# VTM
if( USE_VTM_VIDEO_CODEC )
  file(GLOB SRCVTM
            ${CMAKE_SOURCE_DIR}/dependencies/program-options-lite/program_options_lite_vtm.cpp
            ${CMAKE_SOURCE_DIR}/dependencies/program-options-lite/program_options_lite_vtm.h )
  include_directories( ${VTM_LIB_SOURCE_DIR} )
  set( LIBS ${LIBS} CommonLib EncoderLib_vtm DecoderLib_vtm Utilities_vtm)
endif()

# VVLIB
if( USE_VV_VIDEO_CODEC )
  include_directories( ${CMAKE_SOURCE_DIR}/dependencies/vvenc )
  include_directories( ${CMAKE_SOURCE_DIR}/dependencies/vvenc/source/Lib )
  SET( LIBS ${LIBS} vvenc )
endif()

add_library( ${MYNAME} ${SRC} ${SRCVTM} )

If( MSVC )
  target_link_libraries( ${MYNAME} ${LIBS} ws2_32)
ELSE()
  target_link_libraries( ${MYNAME} ${LIBS} )
endif()

set_target_properties( ${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)
