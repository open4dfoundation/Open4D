get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project(${MYNAME} C CXX)

enable_language(C)

file(GLOB SRC *.cpp *.hpp)

include_directories(${CMAKE_SOURCE_DIR}/dependencies/libmd5
        ${CMAKE_SOURCE_DIR}/dependencies
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementCommon
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
        .)

add_library(${MYNAME} ${SRC} ${CMAKE_SOURCE_DIR}/dependencies/schroedinger/schroarith.c)

target_link_libraries(${MYNAME} acDisplacementBitstream)

set_target_properties(${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)


