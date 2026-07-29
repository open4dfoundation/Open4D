#include <k4a/k4a.hpp>
#include <jsoncpp/json/json.h>
#include <open3d/Open3D.h>
#include <open3d/core/CUDAUtils.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using open3d::core::Device;
using open3d::core::Dtype;
using open3d::core::Tensor;
using open3d::t::geometry::Image;
using open3d::t::geometry::TriangleMesh;
using open3d::t::geometry::VoxelBlockGrid;

const Device kCpu("CPU:0");
const Device kCuda("CUDA:0");

struct Camera {
    uint32_t index;
    std::string serial;
    k4a::device device;
    k4a::calibration calibration;
    Tensor intrinsic;
    Tensor world_to_camera;

    Camera(uint32_t device_index,
           std::string device_serial,
           k4a::device &&opened_device,
           const k4a_device_configuration_t &configuration,
           Tensor extrinsic)
        : index(device_index),
          serial(std::move(device_serial)),
          device(std::move(opened_device)),
          calibration(device.get_calibration(configuration.depth_mode,
                                              configuration.color_resolution)),
          world_to_camera(std::move(extrinsic)) {
        const auto &p = calibration.depth_camera_calibration.intrinsics.parameters.param;
        intrinsic = Tensor::Init<double>({{p.fx, 0.0, p.cx},
                                          {0.0, p.fy, p.cy},
                                          {0.0, 0.0, 1.0}}, kCpu);
    }
};

struct CaptureFrame {
    k4a::capture capture;
    k4a::image depth;
    uint64_t timestamp_usec = 0;
};

Tensor ParseMatrix4(const Json::Value &value) {
    if (!value.isArray() || value.size() != 4) {
        throw std::runtime_error("world_to_camera must be a 4x4 array");
    }
    std::vector<double> data;
    data.reserve(16);
    for (Json::ArrayIndex row = 0; row < 4; ++row) {
        if (!value[row].isArray() || value[row].size() != 4) {
            throw std::runtime_error("world_to_camera must be a 4x4 array");
        }
        for (Json::ArrayIndex col = 0; col < 4; ++col) {
            data.push_back(value[row][col].asDouble());
        }
    }
    Tensor matrix({4, 4}, Dtype::Float64, kCpu);
    std::memcpy(matrix.GetDataPtr(), data.data(), data.size() * sizeof(double));
    return matrix;
}

k4a_wired_sync_mode_t ParseSyncMode(const std::string &value) {
    if (value == "standalone") return K4A_WIRED_SYNC_MODE_STANDALONE;
    if (value == "primary" || value == "master") {
        return K4A_WIRED_SYNC_MODE_MASTER;
    }
    if (value == "subordinate") return K4A_WIRED_SYNC_MODE_SUBORDINATE;
    throw std::runtime_error("sync_mode must be standalone, primary, or subordinate");
}

const char *SyncModeName(k4a_wired_sync_mode_t mode) {
    if (mode == K4A_WIRED_SYNC_MODE_MASTER) return "primary";
    if (mode == K4A_WIRED_SYNC_MODE_SUBORDINATE) return "subordinate";
    return "standalone";
}

CaptureFrame GetFrame(Camera &camera) {
    CaptureFrame frame;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (!camera.device.get_capture(&frame.capture, std::chrono::milliseconds(5000))) {
            std::cerr << "waiting for first valid depth frame from "
                      << camera.serial << " (attempt " << (attempt + 1) << "/6)\n";
            continue;
        }
        frame.depth = frame.capture.get_depth_image();
        if (!frame.depth.is_valid()) continue;
        frame.timestamp_usec = static_cast<uint64_t>(
            frame.depth.get_device_timestamp().count());
        return frame;
    }
    throw std::runtime_error("capture timeout for " + camera.serial);
}

std::vector<CaptureFrame> GetSynchronizedPair(std::vector<Camera> &cameras,
                                               uint64_t max_skew_usec) {
    std::vector<CaptureFrame> frames;
    frames.reserve(cameras.size());
    for (auto &camera : cameras) frames.push_back(GetFrame(camera));

    for (int attempts = 0; attempts < 30; ++attempts) {
        auto min_it = std::min_element(frames.begin(), frames.end(),
            [](const CaptureFrame &a, const CaptureFrame &b) {
                return a.timestamp_usec < b.timestamp_usec;
            });
        auto max_it = std::max_element(frames.begin(), frames.end(),
            [](const CaptureFrame &a, const CaptureFrame &b) {
                return a.timestamp_usec < b.timestamp_usec;
            });
        const uint64_t skew = max_it->timestamp_usec - min_it->timestamp_usec;
        if (skew <= max_skew_usec) return frames;
        const size_t older = static_cast<size_t>(std::distance(frames.begin(), min_it));
        frames[older] = GetFrame(cameras[older]);
    }
    throw std::runtime_error("unable to pair camera timestamps");
}

Image DepthToCudaImage(const k4a::image &depth) {
    const int width = depth.get_width_pixels();
    const int height = depth.get_height_pixels();
    Tensor cpu({height, width, 1}, Dtype::UInt16, kCpu);
    auto *dst = static_cast<uint8_t *>(cpu.GetDataPtr());
    const auto *src = depth.get_buffer();
    const size_t row_bytes = static_cast<size_t>(width) * sizeof(uint16_t);
    const size_t stride = static_cast<size_t>(depth.get_stride_bytes());
    for (int row = 0; row < height; ++row) {
        std::memcpy(dst + static_cast<size_t>(row) * row_bytes,
                    src + static_cast<size_t>(row) * stride,
                    row_bytes);
    }
    return Image(cpu.To(kCuda));
}

std::unique_ptr<VoxelBlockGrid> MakeGrid(float voxel_size, int block_count) {
    return std::make_unique<VoxelBlockGrid>(
        std::vector<std::string>{"tsdf", "weight"},
        std::vector<Dtype>{Dtype::Float32, Dtype::Float32},
        std::vector<open3d::core::SizeVector>{{1}, {1}},
        voxel_size, 16, block_count, kCuda);
}

void Integrate(VoxelBlockGrid &grid,
               const Image &depth,
               const Tensor &intrinsic,
               const Tensor &extrinsic,
               float depth_scale,
               float depth_max,
               float truncation) {
    Tensor blocks = grid.GetUniqueBlockCoordinates(
        depth, intrinsic, extrinsic, depth_scale, depth_max, truncation);
    grid.Integrate(blocks, depth, intrinsic, extrinsic,
                   depth_scale, depth_max, truncation);
}

size_t ExtractAndWrite(VoxelBlockGrid &grid, const std::string &path) {
    TriangleMesh mesh = grid.ExtractTriangleMesh(0.0f, -1).To(kCpu);
    auto legacy = mesh.ToLegacy();
    legacy.ComputeVertexNormals();
    if (!open3d::io::WriteTriangleMesh(path, legacy, true, false, true)) {
        throw std::runtime_error("failed to write " + path);
    }
    return legacy.triangles_.size();
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: " << argv[0] << " <config.dual.json>\n";
            return 2;
        }
        Json::Value config;
        std::ifstream input(argv[1]);
        if (!(input >> config)) throw std::runtime_error("invalid config JSON");
        if (!open3d::core::cuda::IsAvailable()) {
            throw std::runtime_error("Open3D CUDA support is unavailable");
        }
        const Json::Value devices = config["devices"];
        if (!devices.isArray() || devices.empty() || devices.size() > 2) {
            throw std::runtime_error("one or two devices are required");
        }

        std::vector<k4a_device_configuration_t> camera_configs(devices.size());
        for (Json::ArrayIndex wanted = 0; wanted < devices.size(); ++wanted) {
            auto &camera_config = camera_configs[wanted];
            camera_config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
            camera_config.camera_fps = K4A_FRAMES_PER_SECOND_15;
            camera_config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
            camera_config.wired_sync_mode = ParseSyncMode(
                devices[wanted].get("sync_mode", "standalone").asString());
            camera_config.subordinate_delay_off_master_usec =
                devices[wanted].get("subordinate_delay_usec", 0).asInt();
            if (camera_config.wired_sync_mode != K4A_WIRED_SYNC_MODE_STANDALONE) {
                // K4A-compatible synchronization is tied to the color capture
                // cadence, so keep color enabled even though this tool only
                // integrates depth.
                camera_config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
                camera_config.color_resolution = K4A_COLOR_RESOLUTION_720P;
                camera_config.synchronized_images_only = true;
            }
        }

        std::vector<Camera> cameras;
        const uint32_t installed = k4a::device::get_installed_count();
        std::vector<uint32_t> device_indices;
        for (Json::ArrayIndex wanted = 0; wanted < devices.size(); ++wanted) {
            const std::string serial = devices[wanted]["serial"].asString();
            uint32_t device_index = installed;
            if (devices[wanted].isMember("index")) {
                device_index = devices[wanted]["index"].asUInt();
            } else {
                for (uint32_t candidate = 0; candidate < installed; ++candidate) {
                    try {
                        auto probe = k4a::device::open(candidate);
                        if (probe.get_serialnum() == serial) {
                            device_index = candidate;
                            break;
                        }
                    } catch (const std::exception &) {
                        continue;
                    }
                }
            }
            if (device_index >= installed) {
                throw std::runtime_error("device serial " + serial + " is not installed");
            }
            device_indices.push_back(device_index);
        }
        for (Json::ArrayIndex wanted = 0; wanted < devices.size(); ++wanted) {
            const uint32_t device_index = device_indices[wanted];
            const std::string serial = devices[wanted]["serial"].asString();
            auto opened = k4a::device::open(device_index);
            const std::string detected_serial = opened.get_serialnum();
            if (detected_serial != serial) {
                throw std::runtime_error("device index " + std::to_string(device_index) +
                    " is " + detected_serial + ", expected " + serial);
            }
            cameras.emplace_back(device_index, serial, std::move(opened),
                                 camera_configs[wanted],
                                 ParseMatrix4(devices[wanted]["world_to_camera"]));
        }

        // Subordinates must be listening before the primary begins emitting
        // synchronization pulses.
        const k4a_wired_sync_mode_t start_order[] = {
            K4A_WIRED_SYNC_MODE_SUBORDINATE,
            K4A_WIRED_SYNC_MODE_MASTER,
            K4A_WIRED_SYNC_MODE_STANDALONE};
        for (const auto mode : start_order) {
            for (size_t camera_index = 0; camera_index < cameras.size(); ++camera_index) {
                if (camera_configs[camera_index].wired_sync_mode != mode) continue;
                cameras[camera_index].device.start_cameras(&camera_configs[camera_index]);
                std::cout << "started index=" << cameras[camera_index].index
                          << " serial=" << cameras[camera_index].serial
                          << " sync_mode=" << SyncModeName(mode)
                          << " subordinate_delay_us="
                          << camera_configs[camera_index].subordinate_delay_off_master_usec
                          << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }

        const auto &rc = config["reconstruction"];
        const float voxel_size = rc["voxel_size"].asFloat();
        const int block_count = rc["block_count"].asInt();
        const float depth_scale = rc["depth_scale"].asFloat();
        const float depth_max = rc["depth_max"].asFloat();
        const float truncation = rc["trunc_voxel_multiplier"].asFloat();
        auto shared = MakeGrid(voxel_size, block_count);
        std::vector<std::unique_ptr<VoxelBlockGrid>> individual;
        for (Json::ArrayIndex i = 0; i < devices.size(); ++i) {
            individual.push_back(MakeGrid(voxel_size, block_count));
        }

        const int frame_count = config["capture"]["frames"].asInt();
        const uint64_t max_skew = config["capture"]["max_timestamp_skew_usec"].asUInt64();
        for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
            auto frames = GetSynchronizedPair(cameras, max_skew);
            const uint64_t skew = frames.size() == 2
                ? static_cast<uint64_t>(std::llabs(
                      static_cast<long long>(frames[0].timestamp_usec) -
                      static_cast<long long>(frames[1].timestamp_usec)))
                : 0;
            std::cout << "frame=" << frame_index << " skew_us=" << skew << '\n';
            for (size_t camera = 0; camera < cameras.size(); ++camera) {
                Image depth = DepthToCudaImage(frames[camera].depth);
                Integrate(*individual[camera], depth, cameras[camera].intrinsic,
                          Tensor::Eye(4, Dtype::Float64, kCpu),
                          depth_scale, depth_max, truncation);
                Integrate(*shared, depth, cameras[camera].intrinsic,
                          cameras[camera].world_to_camera,
                          depth_scale, depth_max, truncation);
            }
        }

        for (auto &camera : cameras) camera.device.stop_cameras();
        const auto &out = config["output"];
        std::vector<size_t> triangle_counts;
        for (size_t camera = 0; camera < individual.size(); ++camera) {
            const std::string key = "camera_" + std::to_string(camera);
            triangle_counts.push_back(
                ExtractAndWrite(*individual[camera], out[key].asString()));
        }
        const size_t triangles_shared = ExtractAndWrite(*shared, out["shared"].asString());
        std::cout << "triangles";
        for (size_t camera = 0; camera < triangle_counts.size(); ++camera) {
            std::cout << " camera" << camera << '=' << triangle_counts[camera];
        }
        std::cout << " shared=" << triangles_shared << '\n';
        if (devices.size() == 2 && !devices[1]["calibrated"].asBool()) {
            std::cout << "WARNING: shared mesh is uncalibrated; camera 1 extrinsic is identity\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
