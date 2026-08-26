cmake_minimum_required(VERSION 3.17)

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
  include_directories( ${HM_LIB_SOURCE_DIR}/  )
  set( LIBS ${LIBS} TLibCommon TLibDecoder )
endif()

# VTM 
if( USE_VTM_VIDEO_CODEC )
  file(GLOB SRCVTM
            ${CMAKE_SOURCE_DIR}/dependencies/program-options-lite/program_options_lite_vtm.cpp
            ${CMAKE_SOURCE_DIR}/dependencies/program-options-lite/program_options_lite_vtm.h )
  include_directories( ${VTM_LIB_SOURCE_DIR} )
  set( LIBS ${LIBS} CommonLib DecoderLib_vtm )
endif()

# VVLIB
if( USE_VV_VIDEO_CODEC )
  include_directories( ${CMAKE_SOURCE_DIR}/dependencies/vvdec )
  SET( LIBS ${LIBS} vvdec )
endif()

add_library( ${MYNAME}  ${SRC} ${SRCVTM})

If( MSVC )
  target_link_libraries( ${MYNAME} ${LIBS} ws2_32)
ELSE()
  target_link_libraries( ${MYNAME} ${LIBS} )
endif()

set_target_properties( ${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)
