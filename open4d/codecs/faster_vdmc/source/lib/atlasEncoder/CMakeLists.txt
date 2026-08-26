get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project(${MYNAME} C CXX)

enable_language(C)

file(GLOB SRC *.cpp *.hpp)

include_directories(${CMAKE_SOURCE_DIR}/dependencies/libmd5
        ${CMAKE_SOURCE_DIR}/dependencies/
        ${CMAKE_SOURCE_DIR}/source/lib/atlasCommon
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
        .)

add_library(${MYNAME} ${SRC})

target_link_libraries(${MYNAME})

set_target_properties(${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)


