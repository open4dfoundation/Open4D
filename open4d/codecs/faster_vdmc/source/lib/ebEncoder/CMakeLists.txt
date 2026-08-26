get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project( ${MYNAME} C CXX )

enable_language( C )

file(GLOB SRC *.h *.hpp *.cpp )

include_directories( ${CMAKE_SOURCE_DIR}/source/lib/ebBitstream
                     ${CMAKE_SOURCE_DIR}/source/lib/ebCommon
                     ${CMAKE_SOURCE_DIR}/source/lib/ebEncoder
                     ${CMAKE_SOURCE_DIR}/dependencies 
                     ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies
                     ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source
                     ${CMAKE_SOURCE_DIR}/source/lib/ebDecoder )
 
add_library( ${MYNAME} ${SRC} ${CMAKE_SOURCE_DIR}/dependencies/schroedinger/schroarith.c )

target_link_libraries( ${MYNAME} ebCommon ebBitstream ebDecoder)

set_target_properties( ${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)

