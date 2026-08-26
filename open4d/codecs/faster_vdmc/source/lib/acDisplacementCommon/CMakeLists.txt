get_filename_component(MYNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" MYNAME ${MYNAME})
project( ${MYNAME} C CXX )

enable_language( C )

file(GLOB SRC *.cpp *.hpp)

include_directories( ${CMAKE_SOURCE_DIR}/dependencies/libmd5
        . )

add_library( ${MYNAME} ${SRC} )

target_link_libraries( ${MYNAME} )

set_target_properties( ${MYNAME} PROPERTIES LINKER_LANGUAGE CXX)


