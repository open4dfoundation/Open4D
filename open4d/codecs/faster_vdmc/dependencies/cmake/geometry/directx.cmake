# fix policies in dependent projects by injecting code
# notably, set CMP0077 so that setting variables is honoured by subprojects
set(CMAKE_PROJECT_INCLUDE_BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/cmake/tools/fix-policy.cmake)

# Some dependencies include find_library commands, which wont work in the
# in-tree build (because they've not been built yet).  Whereas actually
# there is no need to find anything.  The following fake package definitions
# will satisfy find_library without modifying the submodule
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/cmake/fakepkg)

# Code in directx dependencies may use SAL annotations.
# On non-win32 platforms, these are not defined.  Instead provide a SAL2 stub
if (NOT WIN32)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/sal)
endif()

# directx-mesh unconditionally includes install targets that need dxheaders'
set(DXHEADERS_INSTALL TRUE)

# directx-mesh unconditionally sets BUILD_TOOLS to TRUE, which then affects
# other dependencies that use the same variable.  The tools aren't required.
set (BUILD_TOOLS FALSE)

# directx-headers
set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/directx-headers )
if( NOT EXISTS ${DIR} )
  CPMAddPackage( NAME              directx-headers
                GIT_REPOSITORY    https://github.com/microsoft/DirectX-Headers.git
                GIT_TAG           1b79ddaeabc4b16c772ca63adc5bdf7d5f741460
                SOURCE_DIR        ${DIR} 
                DOWNLOAD_ONLY     YES)
endif()
add_subdirectory(dependencies/directx-headers)

# directx-math
set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/directx-math )
if( NOT EXISTS ${DIR} )
  CPMAddPackage( NAME             directx-math
                GIT_REPOSITORY    https://github.com/microsoft/DirectXMath.git
                GIT_TAG           b404898c9dcaff7b686bbaf6d2fba8ff0184a17e
                SOURCE_DIR        ${DIR} 
                DOWNLOAD_ONLY     YES)
endif()
if( NOT EXISTS ${DIR}/PATCHED )  
  file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/patches/dxmath/*")
  foreach(file ${files})
    execute_process( COMMAND git am ${file} WORKING_DIRECTORY ${DIR} RESULT_VARIABLE ret )
    if( NOT ${ret} EQUAL "0")
      message( FATAL_ERROR "Error during the dxmath patch process. ")
    endif()
  endforeach()
  file( WRITE ${DIR}/PATCHED "patched" )   
else()
  message("directx-dxmath already patched")
endif()
add_subdirectory(dependencies/directx-math)
            
# directx-mesh    
set( DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/directx-mesh )
if( NOT EXISTS ${DIR} )
  CPMAddPackage( NAME             directx-mesh
                GIT_REPOSITORY    https://github.com/microsoft/DirectXMesh.git
                GIT_TAG           2c0ed18e271afa99a70948f784dfe082127fa0de
                SOURCE_DIR        ${DIR} 
                DOWNLOAD_ONLY     YES)
endif()
if( NOT EXISTS ${DIR}/PATCHED )  
  file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/patches/dxmesh/*")
  foreach(file ${files})
    execute_process( COMMAND git am ${file} WORKING_DIRECTORY ${DIR} RESULT_VARIABLE ret )
    if( NOT ${ret} EQUAL "0")
      message( FATAL_ERROR "Error during the dxmesh patch process. ")
    endif()
  endforeach()
  file( WRITE ${DIR}/PATCHED "patched" )   
else()
  message("directx-mesh already patched")
endif()

# disable submodule warnings
if(MSVC)
else()
  add_compile_options(-Wno-unknown-pragmas -Wno-unused-but-set-variable)
endif()

add_subdirectory(dependencies/directx-mesh)