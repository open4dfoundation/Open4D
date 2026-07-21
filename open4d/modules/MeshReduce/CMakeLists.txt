cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(MeshReduce VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local/open3d-0.18" "/usr/local")

find_package(jsoncpp REQUIRED)
find_package(Open3D 0.18.0 REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(Eigen3 3.4 REQUIRED)
find_package(OpenCV REQUIRED
        core imgproc imgcodecs highgui calib3d)
find_package(k4a REQUIRED)
find_package(draco CONFIG REQUIRED)

include_directories(include)

add_subdirectory(src)

add_subdirectory(app)

link_libraries(sensor reconstruction jsoncpp streaming)
