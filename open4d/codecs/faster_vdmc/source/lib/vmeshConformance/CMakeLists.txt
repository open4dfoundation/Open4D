get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project( ${MYNAME} C CXX )

enable_language( C )

file(GLOB SRC *.hpp *.cpp )

include_directories( ${CMAKE_SOURCE_DIR}/dependencies 
                     ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                     ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
                     ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
                     ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream)

add_library( ${MYNAME} ${SRC} ${VERSION_FILE} )

target_link_libraries( ${MYNAME} vmeshCommon )

set_target_properties( ${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)