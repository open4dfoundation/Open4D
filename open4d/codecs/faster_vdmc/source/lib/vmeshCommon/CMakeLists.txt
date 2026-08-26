get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project( ${MYNAME} C CXX )

enable_language( C )

# Determine the software version
set(VERSION_FALLBACK   "v${VDMC_VERSION_MAJOR}.${VDMC_VERSION_MINOR}")
set(VERSION_EXTRA      "" )
set(VERSION_FILE       ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)
set(VERSION_FILE_IN    ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in)
set(VERSION_CACHE_FILE ${CMAKE_CURRENT_BINARY_DIR}/version.cache)
find_package(Git)
add_custom_target(
  genversion
  COMMAND ${CMAKE_COMMAND} -D OUTPUT=${VERSION_FILE}
                           -D TEMPLATE=${VERSION_FILE_IN}
                           -D VERSION_FALLBACK=${VERSION_FALLBACK}
                           -D VERSION_EXTRA=${VERSION_EXTRA}
                           -D VERSION_CACHE_FILE=${VERSION_CACHE_FILE}
                           -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
                           -P ${CMAKE_SOURCE_DIR}/dependencies/cmake/tools/genversion.cmake
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_custom_command( OUTPUT ${VERSION_FILE} DEPENDS genversion )

file(GLOB SRC *.cpp util/*.cpp util/*.hpp *.hpp)

include_directories( ${CMAKE_SOURCE_DIR}/dependencies
                     ${CMAKE_SOURCE_DIR}/dependencies/mmetric/source/lib/include
                     ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/
                     ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/glad/include
                     ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies/glfw/include
                     ${CMAKE_SOURCE_DIR}/dependencies/libmd5/
                     ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source
                     ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
                     ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
                     ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream
                     . )

add_library( ${MYNAME} ${SRC} ${VERSION_FILE} )

target_link_libraries( ${MYNAME} mmlib atlasBitstream basemeshBitstream acDisplacementBitstream)

set_target_properties( ${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)

