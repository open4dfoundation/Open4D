include_directories(${CMAKE_SOURCE_DIR}/include/reconstruction)

add_library(reconstruction
        reconstruction.cpp
        texture_mapping.cpp
)

target_link_libraries(reconstruction
        Open3D::Open3D
        ${OpenCV_LIBS}
        jsoncpp
        Eigen3::Eigen
)
