if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kinect_streamer.cpp")
    add_executable(kinect_streamer kinect_streamer.cpp)
    target_link_libraries(kinect_streamer PUBLIC sensor streaming jsoncpp)
endif()

add_executable(sensor_client
        sensor_client.cpp
)

target_link_libraries(sensor_client
        PUBLIC
        sensor
        reconstruction
        streaming
        jsoncpp
        Open3D::Open3D
        CUDA::cudart_static
)

add_executable(dual_camera_fusion dual_camera_fusion.cpp)
target_link_libraries(dual_camera_fusion
        PRIVATE
        k4a::k4a
        Open3D::Open3D
        jsoncpp
        CUDA::cudart_static
)

add_executable(rgbd_streamer rgbd_streamer.cpp)
target_link_libraries(rgbd_streamer
        PRIVATE
        k4a::k4a
        Open3D::Open3D
        ${OpenCV_LIBS}
        jsoncpp
        draco::draco
        CUDA::cudart_static
)
