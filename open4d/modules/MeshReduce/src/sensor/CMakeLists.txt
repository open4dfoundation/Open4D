find_package(k4a REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include/sensor)
include_directories(${CMAKE_SOURCE_DIR}/include/)

add_library(sensor
        kinect_capture.cpp
        kinect_cameras.cpp
        ../util.cpp
)

target_link_libraries(sensor
        k4a::k4a
        Open3D::Open3D
        ${OpenCV_LIBS}
        jsoncpp
        Eigen3::Eigen
)
