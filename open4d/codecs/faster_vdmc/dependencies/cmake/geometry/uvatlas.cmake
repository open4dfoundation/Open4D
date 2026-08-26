set (UVATLAS_USE_OPENMP FALSE)

set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/uvatlas )
if( NOT EXISTS ${DIR} )
  CPMAddPackage( NAME             uvatlas
                GIT_REPOSITORY    https://github.com/microsoft/UVAtlas.git
                GIT_TAG           5af1b5d2a0fd9e0e5d17aa0971ab17c890e318e0
                SOURCE_DIR        ${DIR}
                DOWNLOAD_ONLY     YES)  
endif()

if( NOT EXISTS ${DIR}/PATCHED )  
  file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/patches/uvatlas/*")
  foreach(file ${files})
    execute_process( COMMAND git am --ignore-whitespace ${file}  WORKING_DIRECTORY ${DIR} RESULT_VARIABLE ret )
    if( NOT ${ret} EQUAL "0")
      message( FATAL_ERROR "Error during the uvatlas patch process. ")
    endif()
  endforeach()
  file( WRITE ${DIR}/PATCHED "patched" )   
else()
  message("uvatlas already patched")
endif()

# disable submodule warnings
if(MSVC)
  add_definitions("/wd4005 /wd4244")
else()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-unused-value
                        -Wno-sequence-point
                        -Wno-deprecated-copy-with-user-provided-copy
                        -Wno-unused-but-set-variable)
  else()
    add_compile_options(-Wno-unused-value
                        -Wno-sequence-point
                        -Wno-class-memaccess)
  endif()
endif()

add_subdirectory(dependencies/uvatlas)


