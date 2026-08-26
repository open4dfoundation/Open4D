get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project(${MYNAME} C CXX)

enable_language(C)
find_package(Threads REQUIRED)

file(GLOB SRC *.h *.cpp *.hpp)

include_directories(${CMAKE_SOURCE_DIR}/dependencies
        ${CMAKE_SOURCE_DIR}/dependencies/uvatlas/UVAtlas/inc
        ${CMAKE_SOURCE_DIR}/dependencies/directx-math/Inc
        ${CMAKE_SOURCE_DIR}/dependencies/directx-mesh/DirectXMesh
        ${CMAKE_SOURCE_DIR}/dependencies/directx-headers/include
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/atlasCommon
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
        ${CMAKE_SOURCE_DIR}/source/wrapper/videoEncoder
        ${CMAKE_SOURCE_DIR}/source/wrapper/geometryEncoder
        ${CMAKE_SOURCE_DIR}/source/wrapper/colourConverter
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/source/lib/include
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/glad/include
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/glfw/include
        ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source
        ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/eigen3)

add_library(${MYNAME} ${SRC} ${CMAKE_SOURCE_DIR}/dependencies/schroedinger/schroarith.c)

target_link_libraries(${MYNAME} vmeshCommon geometryEncoder videoEncoder colourConverter atlasEncoder atlasDecoder basemeshEncoder acDisplacementEncoder bitstreamsCommon Threads::Threads)

set_target_properties(${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)

if (${USE_SHMAPP_VIDEO_CODEC})
    if (WIN32)
        set(SHM_ENC_PATH "${CMAKE_SOURCE_DIR}/dependencies/SHM-12.4/bin/TAppEncoder.exe")
    else ()
        set(SHM_ENC_PATH "${CMAKE_SOURCE_DIR}/dependencies/SHM-12.4/bin/TAppEncoderStatic")
    endif ()
    add_compile_definitions(SHM_ENC_PATH="${SHM_ENC_PATH}")
endif ()
