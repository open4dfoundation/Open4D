
set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tinyply )
if( NOT EXISTS ${DIR} )
  CPMAddPackage( NAME             tinyply
                GIT_REPOSITORY    https://github.com/ddiakopoulos/tinyply.git
                GIT_TAG           2.3.4
                SOURCE_DIR        ${DIR} 
                DOWNLOAD_ONLY     YES)
endif()