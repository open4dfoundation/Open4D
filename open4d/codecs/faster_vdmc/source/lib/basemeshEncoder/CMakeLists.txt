get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project(${MYNAME} C CXX)

enable_language(C)

file(GLOB SRC *.cpp *.hpp)

include_directories(${CMAKE_SOURCE_DIR}/dependencies/libmd5
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream/
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/wrapper/geometryEncoder
        ${CMAKE_SOURCE_DIR}/dependencies
        ${CMAKE_SOURCE_DIR}/dependencies/uvatlas/UVAtlas/inc
        ${CMAKE_SOURCE_DIR}/dependencies/directx-math/Inc
        ${CMAKE_SOURCE_DIR}/dependencies/directx-mesh/DirectXMesh
        ${CMAKE_SOURCE_DIR}/dependencies/directx-headers/include

        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/source/lib/include
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/glad/include
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/glfw/include
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/eigen3
        ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source
        .)

add_library(${MYNAME} ${SRC} ${CMAKE_SOURCE_DIR}/dependencies/schroedinger/schroarith.c)

target_link_libraries(${MYNAME} geometryEncoder)

set_target_properties(${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)


