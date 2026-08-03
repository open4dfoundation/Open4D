cmake_minimum_required(VERSION 3.1)
SET(CMAKE_BUILD_TYPE release)
project(ssp_remesh)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# libigl
option(LIBIGL_WITH_OPENGL            "Use OpenGL"         ON)
option(LIBIGL_WITH_OPENGL_GLFW       "Use GLFW"           ON)

find_package(LIBIGL REQUIRED)

# Add your project files
include_directories("./src/")

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "dir='${dir}'")
endforeach()

file(GLOB SRCFILES *.cpp ./src/*.cpp)
add_executable(${PROJECT_NAME}_bin ${SRCFILES})

target_link_libraries(${PROJECT_NAME}_bin igl::core igl::opengl igl::opengl_glfw)



