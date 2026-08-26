get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project(${MYNAME} C CXX)

enable_language(C)
find_package(Threads REQUIRED)

file(GLOB SRC *.hpp *.cpp)

include_directories(${CMAKE_SOURCE_DIR}/dependencies
        ${CMAKE_SOURCE_DIR}/source/lib/atlasDecoder
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream/
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream/syntaxElements/
        ${CMAKE_SOURCE_DIR}/source/lib/atlasCommon
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshDecoder
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshConformance
        ${CMAKE_SOURCE_DIR}/source/wrapper/videoDecoder
        ${CMAKE_SOURCE_DIR}/source/wrapper/geometryDecoder
        ${CMAKE_SOURCE_DIR}/source/wrapper/colourConverter
        ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source
        ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream/
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon/
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream/
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream/syntaxElements/
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementCommon)

add_library(${MYNAME} ${SRC} ${CMAKE_SOURCE_DIR}/dependencies/schroedinger/schroarith.c)

target_link_libraries(${MYNAME} vmeshCommon geometryDecoder videoDecoder colourConverter vmeshConformance atlasDecoder basemeshDecoder acDisplacementDecoder acDisplacementBitstream bitstreamsCommon Threads::Threads)

set_target_properties(${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)

if (${USE_SHMAPP_VIDEO_CODEC})
    if (WIN32)
        set(SHM_DEC_PATH "${CMAKE_SOURCE_DIR}/dependencies/SHM-12.4/bin/TAppEncoder.exe")
    else ()
        set(SHM_DEC_PATH "${CMAKE_SOURCE_DIR}/dependencies/SHM-12.4/bin/TAppDecoderStatic")
    endif ()
    add_compile_definitions(SHM_DEC_PATH="${SHM_DEC_PATH}")
endif ()
