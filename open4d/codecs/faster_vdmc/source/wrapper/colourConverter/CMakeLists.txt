cmake_minimum_required(VERSION 3.17)

get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string( REPLACE " " "_" MYNAME ${MYNAME})
project(${MYNAME} C CXX)

file(GLOB SRC *.h *.cpp  
              ${CMAKE_SOURCE_DIR}/dependencies/hdrtools/projects/HDRConvert/inc/ProjectParameters.h
              ${CMAKE_SOURCE_DIR}/dependencies/hdrtools/projects/HDRConvert/src/ProjectParameters.cpp )

include_directories( ${CMAKE_SOURCE_DIR}/dependencies 
                     ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                     ${CMAKE_SOURCE_DIR}/dependencies/hdrtools/common/inc
                     ${CMAKE_SOURCE_DIR}/dependencies/hdrtools/projects/HDRConvert/inc )

set( LIBS vmeshCommon )
if( USE_HDRTOOLS )
  set( LIBS ${LIBS} HDRLib )
endif()

add_library( ${MYNAME} ${SRC} )

target_link_libraries( ${MYNAME} ${LIBS} )

set_target_properties( ${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)