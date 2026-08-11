# 3.1 was the original minimum, and CMake 4.0 removed compatibility with
# anything below 3.5, so that value now fails to configure outright rather than
# merely warning. 3.16 is the floor Ubuntu 24.04 and Homebrew both clear.
cmake_minimum_required(VERSION 3.16)
SET(CMAKE_BUILD_TYPE release)
project(ssp_remesh)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Eigen, if the system has it. libigl only downloads its own copy when
# Eigen3::Eigen does not already exist, and letting it download is what fails on
# CMake 4: the fetch runs through libigl's DownloadProject, which generates a
# helper project pinned at `cmake_minimum_required(VERSION 3.2)` in a nested
# cmake invocation that forwards no policy flags. A system Eigen skips all of
# that. Install libeigen3-dev (apt) or eigen (brew).
find_package(Eigen3 3.3 QUIET NO_MODULE)

# libigl. OpenGL and GLFW are deliberately not requested: this is a headless
# command-line remesher, and asking for them made the build pull in glad and
# glfw -- both added with add_subdirectory, both declaring a pre-3.5 minimum --
# for a viewer no source file here constructs.
find_package(LIBIGL REQUIRED)

# Add your project files
include_directories("./src/")

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "dir='${dir}'")
endforeach()

file(GLOB SRCFILES *.cpp ./src/*.cpp)
add_executable(${PROJECT_NAME}_bin ${SRCFILES})

target_link_libraries(${PROJECT_NAME}_bin igl::core)



