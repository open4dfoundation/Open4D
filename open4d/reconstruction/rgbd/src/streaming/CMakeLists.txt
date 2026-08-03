include_directories(${CMAKE_SOURCE_DIR}/include/streaming)

add_library(streaming
        network_stream.cpp
        sender_to_unity.cpp
        send_image.cpp
)

target_link_libraries(streaming
        Open3D::Open3D
        ${OpenCV_LIBS}
        Eigen3::Eigen
)
