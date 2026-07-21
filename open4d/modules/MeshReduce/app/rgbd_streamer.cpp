#include <k4a/k4a.hpp>
#include <jsoncpp/json/json.h>
#include <open3d/Open3D.h>
#include <open3d/core/CUDAUtils.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <draco/attributes/geometry_attribute.h>
#include <draco/compression/encode.h>
#include <draco/core/encoder_buffer.h>
#include <draco/mesh/mesh.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using open3d::core::Device;
using open3d::core::Dtype;
using open3d::core::Tensor;
using open3d::t::geometry::Image;
using open3d::t::geometry::VoxelBlockGrid;

const Device kCpu("CPU:0");
const Device kCuda("CUDA:0");

struct Frame {
    k4a::image color;
    k4a::image depth_in_color;
};

class SingleFrameQueue {
public:
    void Put(Frame frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        writable_.wait(lock, [&] { return !frame_.has_value() || stopped_; });
        if (stopped_) return;
        frame_ = std::move(frame);
        readable_.notify_one();
    }

    bool Get(Frame &frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        readable_.wait(lock, [&] { return frame_.has_value() || stopped_; });
        if (!frame_.has_value()) return false;
        frame = std::move(*frame_);
        frame_.reset();
        writable_.notify_one();
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        readable_.notify_all();
        writable_.notify_all();
    }

private:
    std::optional<Frame> frame_;
    bool stopped_ = false;
    std::mutex mutex_;
    std::condition_variable readable_;
    std::condition_variable writable_;
};

k4a_color_resolution_t ParseColorResolution(const std::string &value) {
    if (value == "720p") return K4A_COLOR_RESOLUTION_720P;
    if (value == "1080p") return K4A_COLOR_RESOLUTION_1080P;
    if (value == "1440p") return K4A_COLOR_RESOLUTION_1440P;
    if (value == "1536p") return K4A_COLOR_RESOLUTION_1536P;
    if (value == "2160p") return K4A_COLOR_RESOLUTION_2160P;
    if (value == "3072p") return K4A_COLOR_RESOLUTION_3072P;
    throw std::runtime_error("unsupported color_resolution: " + value);
}

k4a_depth_mode_t ParseDepthMode(const std::string &value) {
    if (value == "nfov_unbinned") return K4A_DEPTH_MODE_NFOV_UNBINNED;
    if (value == "nfov_2x2binned") return K4A_DEPTH_MODE_NFOV_2X2BINNED;
    if (value == "wfov_unbinned") return K4A_DEPTH_MODE_WFOV_UNBINNED;
    if (value == "wfov_2x2binned") return K4A_DEPTH_MODE_WFOV_2X2BINNED;
    throw std::runtime_error("unsupported depth_mode: " + value);
}

k4a_fps_t ParseFps(int fps) {
    if (fps == 5) return K4A_FRAMES_PER_SECOND_5;
    if (fps == 15) return K4A_FRAMES_PER_SECOND_15;
    if (fps == 30) return K4A_FRAMES_PER_SECOND_30;
    throw std::runtime_error("fps must be 5, 15, or 30");
}

uint32_t ResolveDevice(const Json::Value &config) {
    const uint32_t installed = k4a::device::get_installed_count();
    if (installed == 0) throw std::runtime_error("no K4A-compatible camera detected");
    const std::string wanted = config["serial"].asString();
    if (!wanted.empty()) {
        for (uint32_t index = 0; index < installed; ++index) {
            try {
                auto probe = k4a::device::open(index);
                if (probe.get_serialnum() == wanted) return index;
            } catch (const std::exception &) {
            }
        }
        throw std::runtime_error("camera serial " + wanted + " is not installed");
    }
    const uint32_t index = config.get("index", 0).asUInt();
    if (index >= installed) throw std::runtime_error("camera index is not installed");
    return index;
}

Image DepthToCuda(const k4a::image &depth) {
    const int width = depth.get_width_pixels();
    const int height = depth.get_height_pixels();
    Tensor cpu({height, width, 1}, Dtype::UInt16, kCpu);
    auto *destination = static_cast<uint8_t *>(cpu.GetDataPtr());
    const size_t row_bytes = static_cast<size_t>(width) * sizeof(uint16_t);
    for (int row = 0; row < height; ++row) {
        std::memcpy(destination + static_cast<size_t>(row) * row_bytes,
                    depth.get_buffer() + static_cast<size_t>(row) * depth.get_stride_bytes(),
                    row_bytes);
    }
    return Image(cpu.To(kCuda));
}

cv::Mat ColorToRgb(const k4a::image &color) {
    cv::Mat bgra(color.get_height_pixels(), color.get_width_pixels(), CV_8UC4,
                 const_cast<uint8_t *>(color.get_buffer()), color.get_stride_bytes());
    cv::Mat rgb;
    cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2RGB);
    return rgb;
}

Tensor MakeIntrinsic(const k4a::calibration &calibration) {
    const auto &p = calibration.color_camera_calibration.intrinsics.parameters.param;
    return Tensor::Init<double>({{p.fx, 0.0, p.cx},
                                 {0.0, p.fy, p.cy},
                                 {0.0, 0.0, 1.0}}, kCpu);
}

std::unique_ptr<VoxelBlockGrid> MakeGrid(float voxel_size, int block_count) {
    return std::make_unique<VoxelBlockGrid>(
        std::vector<std::string>{"tsdf", "weight"},
        std::vector<Dtype>{Dtype::Float32, Dtype::Float32},
        std::vector<open3d::core::SizeVector>{{1}, {1}},
        voxel_size, 16, block_count, kCuda);
}

void Integrate(VoxelBlockGrid &grid, const Image &depth, const Tensor &intrinsic,
               float depth_scale, float depth_max, float truncation) {
    const Tensor extrinsic = Tensor::Eye(4, Dtype::Float64, kCpu);
    Tensor blocks = grid.GetUniqueBlockCoordinates(
        depth, intrinsic, extrinsic, depth_scale, depth_max, truncation);
    grid.Integrate(blocks, depth, intrinsic, extrinsic,
                   depth_scale, depth_max, truncation);
}

std::vector<Eigen::Vector2d> ProjectUvs(
    const open3d::geometry::TriangleMesh &mesh,
    const k4a::calibration &calibration,
    int width,
    int height) {
    const auto &p = calibration.color_camera_calibration.intrinsics.parameters.param;
    std::vector<Eigen::Vector2d> uvs;
    uvs.reserve(mesh.triangles_.size() * 3);
    for (const auto &triangle : mesh.triangles_) {
        for (int corner = 0; corner < 3; ++corner) {
            const Eigen::Vector3d &point = mesh.vertices_.at(triangle(corner));
            double u = 0.0;
            double v = 0.0;
            if (point.z() > 0.0) {
                u = (p.fx * point.x() / point.z() + p.cx) / width;
                v = 1.0 - (p.fy * point.y() / point.z() + p.cy) / height;
            }
            uvs.emplace_back(std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0));
        }
    }
    return uvs;
}

std::string Basename(const std::string &path) {
    return std::filesystem::path(path).filename().string();
}

void WriteTexturedObj(const std::string &prefix,
                      open3d::geometry::TriangleMesh &mesh,
                      const std::vector<Eigen::Vector2d> &uvs,
                      const cv::Mat &rgb_texture) {
    const std::string obj_path = prefix + ".obj";
    const std::string mtl_path = prefix + ".mtl";
    const std::string texture_path = prefix + "_texture.png";
    std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());

    cv::Mat bgr;
    cv::cvtColor(rgb_texture, bgr, cv::COLOR_RGB2BGR);
    if (!cv::imwrite(texture_path, bgr)) {
        throw std::runtime_error("failed to write texture " + texture_path);
    }

    std::ofstream material(mtl_path);
    material << "newmtl meshreduce_material\n"
             << "Ka 1.0 1.0 1.0\nKd 1.0 1.0 1.0\nKs 0.0 0.0 0.0\n"
             << "map_Kd " << Basename(texture_path) << "\n";
    if (!material) throw std::runtime_error("failed to write " + mtl_path);

    mesh.ComputeVertexNormals();
    std::ofstream obj(obj_path);
    obj << std::setprecision(9);
    obj << "mtllib " << Basename(mtl_path) << "\nusemtl meshreduce_material\n";
    for (const auto &vertex : mesh.vertices_) {
        obj << "v " << vertex.x() << ' ' << vertex.y() << ' ' << vertex.z() << '\n';
    }
    for (const auto &normal : mesh.vertex_normals_) {
        obj << "vn " << normal.x() << ' ' << normal.y() << ' ' << normal.z() << '\n';
    }
    for (const auto &uv : uvs) obj << "vt " << uv.x() << ' ' << uv.y() << '\n';
    size_t uv_index = 1;
    for (const auto &triangle : mesh.triangles_) {
        obj << "f";
        for (int corner = 0; corner < 3; ++corner) {
            const size_t vertex_index = static_cast<size_t>(triangle(corner)) + 1;
            obj << ' ' << vertex_index << '/' << uv_index++ << '/' << vertex_index;
        }
        obj << '\n';
    }
    if (!obj) throw std::runtime_error("failed to write " + obj_path);
}

void SendAll(int socket_fd, const void *data, size_t bytes) {
    const auto *cursor = static_cast<const uint8_t *>(data);
    while (bytes > 0) {
        const ssize_t sent = send(socket_fd, cursor, bytes, MSG_NOSIGNAL);
        if (sent <= 0) throw std::runtime_error("TCP send failed");
        cursor += sent;
        bytes -= static_cast<size_t>(sent);
    }
}

std::vector<uint8_t> EncodeDraco(
    const open3d::geometry::TriangleMesh &mesh,
    const std::vector<Eigen::Vector2d> &corner_uvs,
    const Json::Value &options) {
    if (corner_uvs.size() != mesh.triangles_.size() * 3) {
        throw std::runtime_error("UV count does not match the triangle corners");
    }

    draco::Mesh encoded_mesh;
    encoded_mesh.set_num_points(static_cast<uint32_t>(mesh.vertices_.size()));

    draco::GeometryAttribute definition;
    definition.Init(draco::GeometryAttribute::POSITION, nullptr, 3,
                    draco::DT_FLOAT32, false, sizeof(float) * 3, 0);
    const int position_id = encoded_mesh.AddAttribute(
        definition, true, static_cast<uint32_t>(mesh.vertices_.size()));
    definition.Init(draco::GeometryAttribute::NORMAL, nullptr, 3,
                    draco::DT_FLOAT32, false, sizeof(float) * 3, 0);
    const int normal_id = encoded_mesh.AddAttribute(
        definition, true, static_cast<uint32_t>(mesh.vertices_.size()));
    definition.Init(draco::GeometryAttribute::TEX_COORD, nullptr, 2,
                    draco::DT_FLOAT32, false, sizeof(float) * 2, 0);
    const int uv_id = encoded_mesh.AddAttribute(
        definition, true, static_cast<uint32_t>(mesh.vertices_.size()));
    if (position_id < 0 || normal_id < 0 || uv_id < 0) {
        throw std::runtime_error("failed to create Draco mesh attributes");
    }

    std::vector<std::array<float, 2>> vertex_uvs(mesh.vertices_.size(), {0.0f, 0.0f});
    for (size_t face_index = 0; face_index < mesh.triangles_.size(); ++face_index) {
        draco::Mesh::Face face;
        for (int corner = 0; corner < 3; ++corner) {
            const uint32_t vertex = static_cast<uint32_t>(
                mesh.triangles_[face_index](corner));
            face[corner] = draco::PointIndex(vertex);
            const Eigen::Vector2d &uv = corner_uvs[face_index * 3 + corner];
            vertex_uvs[vertex] = {static_cast<float>(uv.x()),
                                  static_cast<float>(uv.y())};
        }
        encoded_mesh.AddFace(face);
    }

    auto *positions = encoded_mesh.attribute(position_id);
    auto *normals = encoded_mesh.attribute(normal_id);
    auto *tex_coords = encoded_mesh.attribute(uv_id);
    for (uint32_t index = 0; index < mesh.vertices_.size(); ++index) {
        const auto &vertex = mesh.vertices_[index];
        const auto &normal = mesh.vertex_normals_[index];
        const std::array<float, 3> position_value = {
            static_cast<float>(vertex.x()), static_cast<float>(vertex.y()),
            static_cast<float>(vertex.z())};
        const std::array<float, 3> normal_value = {
            static_cast<float>(normal.x()), static_cast<float>(normal.y()),
            static_cast<float>(normal.z())};
        positions->SetAttributeValue(draco::AttributeValueIndex(index),
                                     position_value.data());
        normals->SetAttributeValue(draco::AttributeValueIndex(index),
                                   normal_value.data());
        tex_coords->SetAttributeValue(draco::AttributeValueIndex(index),
                                      vertex_uvs[index].data());
    }

    draco::Encoder encoder;
    encoder.SetSpeedOptions(options.get("encoding_speed", 5).asInt(),
                            options.get("decoding_speed", 5).asInt());
    encoder.SetAttributeQuantization(
        draco::GeometryAttribute::POSITION,
        options.get("position_quantization_bits", 11).asInt());
    encoder.SetAttributeQuantization(
        draco::GeometryAttribute::NORMAL,
        options.get("normal_quantization_bits", 8).asInt());
    encoder.SetAttributeQuantization(
        draco::GeometryAttribute::TEX_COORD,
        options.get("texcoord_quantization_bits", 10).asInt());
    draco::EncoderBuffer buffer;
    const draco::Status status = encoder.EncodeMeshToBuffer(encoded_mesh, &buffer);
    if (!status.ok()) {
        throw std::runtime_error("Draco encoding failed: " +
                                 status.error_msg_string());
    }
    return std::vector<uint8_t>(buffer.data(), buffer.data() + buffer.size());
}

void SendRawFrame(const Json::Value &network,
                  const open3d::geometry::TriangleMesh &mesh,
                  const std::vector<Eigen::Vector2d> &uvs,
                  const cv::Mat &rgb_texture) {
    const std::string address = network.get("bind_address", "127.0.0.1").asString();
    const uint16_t port = static_cast<uint16_t>(network.get("port", 33669).asUInt());
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) throw std::runtime_error("failed to create TCP listener");
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) != 0 ||
        listen(listener, 1) != 0) {
        close(listener);
        throw std::runtime_error("failed to bind TCP listener " + address + ":" +
                                 std::to_string(port));
    }
    std::cout << "waiting for MRD1 client on " << address << ':' << port << '\n';
    int client = accept(listener, nullptr, nullptr);
    close(listener);
    if (client < 0) throw std::runtime_error("TCP accept failed");

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint32_t> indices;
    std::vector<float> uv_values;
    positions.reserve(mesh.vertices_.size() * 3);
    normals.reserve(mesh.vertex_normals_.size() * 3);
    indices.reserve(mesh.triangles_.size() * 3);
    uv_values.reserve(uvs.size() * 2);
    for (const auto &v : mesh.vertices_) {
        positions.insert(positions.end(), {static_cast<float>(v.x()),
                                           static_cast<float>(v.y()),
                                           static_cast<float>(v.z())});
    }
    for (const auto &n : mesh.vertex_normals_) {
        normals.insert(normals.end(), {static_cast<float>(n.x()),
                                       static_cast<float>(n.y()),
                                       static_cast<float>(n.z())});
    }
    for (const auto &f : mesh.triangles_) {
        indices.insert(indices.end(), {static_cast<uint32_t>(f.x()),
                                       static_cast<uint32_t>(f.y()),
                                       static_cast<uint32_t>(f.z())});
    }
    for (const auto &uv : uvs) {
        uv_values.insert(uv_values.end(), {static_cast<float>(uv.x()),
                                           static_cast<float>(uv.y())});
    }

    constexpr uint32_t kMagic = 0x4d524431;
    uint32_t header[11] = {
        kMagic, 1,
        static_cast<uint32_t>(mesh.vertices_.size()),
        static_cast<uint32_t>(mesh.triangles_.size()),
        static_cast<uint32_t>(positions.size() * sizeof(float)),
        static_cast<uint32_t>(normals.size() * sizeof(float)),
        static_cast<uint32_t>(indices.size() * sizeof(uint32_t)),
        static_cast<uint32_t>(uv_values.size() * sizeof(float)),
        static_cast<uint32_t>(rgb_texture.cols),
        static_cast<uint32_t>(rgb_texture.rows),
        static_cast<uint32_t>(rgb_texture.total() * rgb_texture.elemSize())};
    for (auto &value : header) value = htonl(value);
    SendAll(client, header, sizeof(header));
    SendAll(client, positions.data(), positions.size() * sizeof(float));
    SendAll(client, normals.data(), normals.size() * sizeof(float));
    SendAll(client, indices.data(), indices.size() * sizeof(uint32_t));
    SendAll(client, uv_values.data(), uv_values.size() * sizeof(float));
    SendAll(client, rgb_texture.data, rgb_texture.total() * rgb_texture.elemSize());
    close(client);
    std::cout << "sent one MRD1 mesh/texture frame\n";
}

void SendDracoFrame(const Json::Value &network,
                    const std::vector<uint8_t> &draco_mesh,
                    const cv::Mat &rgb_texture) {
    const std::string address = network.get("bind_address", "127.0.0.1").asString();
    const uint16_t port = static_cast<uint16_t>(network.get("port", 33669).asUInt());
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) throw std::runtime_error("failed to create TCP listener");
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) != 0 ||
        listen(listener, 1) != 0) {
        close(listener);
        throw std::runtime_error("failed to bind TCP listener " + address + ":" +
                                 std::to_string(port));
    }
    std::cout << "waiting for MRD2 (Draco) client on " << address << ':' << port
              << '\n';
    const int client = accept(listener, nullptr, nullptr);
    close(listener);
    if (client < 0) throw std::runtime_error("TCP accept failed");

    constexpr uint32_t kMagic = 0x4d524432;
    constexpr uint32_t kRawRgb8 = 1;
    uint32_t header[7] = {
        kMagic, 1, static_cast<uint32_t>(draco_mesh.size()),
        static_cast<uint32_t>(rgb_texture.cols),
        static_cast<uint32_t>(rgb_texture.rows),
        static_cast<uint32_t>(rgb_texture.total() * rgb_texture.elemSize()),
        kRawRgb8};
    for (auto &value : header) value = htonl(value);
    SendAll(client, header, sizeof(header));
    SendAll(client, draco_mesh.data(), draco_mesh.size());
    SendAll(client, rgb_texture.data,
            rgb_texture.total() * rgb_texture.elemSize());
    close(client);
    std::cout << "sent one MRD2 Draco mesh/raw RGB texture frame\n";
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: " << argv[0] << " <config.rgbd.json>\n";
            return 2;
        }
        Json::Value config;
        std::ifstream input(argv[1]);
        if (!(input >> config)) throw std::runtime_error("invalid config JSON");
        if (!open3d::core::cuda::IsAvailable()) {
            throw std::runtime_error("Open3D CUDA support is unavailable");
        }

        const Json::Value capture = config["capture"];
        k4a_device_configuration_t camera_config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
        camera_config.camera_fps = ParseFps(capture.get("fps", 15).asInt());
        camera_config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
        camera_config.color_resolution = ParseColorResolution(
            capture.get("color_resolution", "1080p").asString());
        camera_config.depth_mode = ParseDepthMode(
            capture.get("depth_mode", "nfov_unbinned").asString());
        camera_config.synchronized_images_only = true;
        camera_config.wired_sync_mode = K4A_WIRED_SYNC_MODE_STANDALONE;

        const uint32_t device_index = ResolveDevice(config["device"]);
        auto device = k4a::device::open(device_index);
        const std::string serial = device.get_serialnum();
        const auto calibration = device.get_calibration(
            camera_config.depth_mode, camera_config.color_resolution);
        const Tensor intrinsic = MakeIntrinsic(calibration);
        auto transformation = k4a::transformation(calibration);
        device.start_cameras(&camera_config);
        std::cout << "started RGB-D camera index=" << device_index
                  << " serial=" << serial << '\n';

        SingleFrameQueue queue;
        std::exception_ptr capture_error;
        const int frame_count = capture.get("frames", 10).asInt();
        const int attempts = capture.get("startup_attempts", 6).asInt();
        const int timeout_ms = capture.get("timeout_ms", 5000).asInt();
        std::thread producer([&] {
            try {
                int produced = 0;
                int failures = 0;
                while (produced < frame_count) {
                    k4a::capture captured;
                    if (!device.get_capture(&captured, std::chrono::milliseconds(timeout_ms))) {
                        if (++failures >= attempts) {
                            throw std::runtime_error("timed out waiting for RGB-D capture");
                        }
                        std::cerr << "waiting for valid RGB-D frame attempt="
                                  << failures << '/' << attempts << '\n';
                        continue;
                    }
                    k4a::image color = captured.get_color_image();
                    k4a::image depth = captured.get_depth_image();
                    if (!color.is_valid() || !depth.is_valid()) continue;
                    k4a::image depth_in_color =
                        transformation.depth_image_to_color_camera(depth);
                    if (!depth_in_color.is_valid()) continue;
                    queue.Put({std::move(color), std::move(depth_in_color)});
                    ++produced;
                }
            } catch (...) {
                capture_error = std::current_exception();
            }
            queue.Stop();
        });

        const Json::Value reconstruction = config["reconstruction"];
        auto grid = MakeGrid(reconstruction.get("voxel_size", 0.01).asFloat(),
                             reconstruction.get("block_count", 20000).asInt());
        cv::Mat last_texture;
        int integrated = 0;
        Frame frame;
        while (queue.Get(frame)) {
            Image depth = DepthToCuda(frame.depth_in_color);
            Integrate(*grid, depth, intrinsic,
                      reconstruction.get("depth_scale", 1000.0).asFloat(),
                      reconstruction.get("depth_max", 3.0).asFloat(),
                      reconstruction.get("trunc_voxel_multiplier", 4.0).asFloat());
            last_texture = ColorToRgb(frame.color).clone();
            std::cout << "integrated RGB-D frame=" << integrated++ << '\n';
        }
        producer.join();
        device.stop_cameras();
        if (capture_error) std::rethrow_exception(capture_error);
        if (integrated == 0 || last_texture.empty()) {
            throw std::runtime_error("no RGB-D frames were integrated");
        }

        auto tensor_mesh = grid->ExtractTriangleMesh(0.0f, -1).To(kCpu);
        auto mesh = tensor_mesh.ToLegacy();
        mesh.RemoveDuplicatedVertices();
        mesh.RemoveDuplicatedTriangles();
        mesh.RemoveDegenerateTriangles();
        mesh.RemoveUnreferencedVertices();
        const size_t original_faces = mesh.triangles_.size();

        const Json::Value reduction = config["reduction"];
        if (reduction.get("enabled", false).asBool() && original_faces > 4) {
            const double target_reduction = std::clamp(
                reduction.get("target_reduction", 0.5).asDouble(), 0.0, 0.99);
            const size_t target_faces = std::max<size_t>(
                4, static_cast<size_t>(original_faces * (1.0 - target_reduction)));
            mesh = *mesh.SimplifyQuadricDecimation(
                static_cast<int>(target_faces),
                std::numeric_limits<double>::infinity(), 1.0);
            mesh.RemoveDegenerateTriangles();
            mesh.RemoveUnreferencedVertices();
        }
        mesh.ComputeVertexNormals();
        const auto uvs = ProjectUvs(mesh, calibration,
                                    last_texture.cols, last_texture.rows);

        const std::string prefix = config["output"].get(
            "prefix", "/tmp/meshreduce_rgbd").asString();
        std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
        if (!open3d::io::WriteTriangleMesh(prefix + ".ply", mesh, true, false, true)) {
            throw std::runtime_error("failed to write PLY output");
        }
        WriteTexturedObj(prefix, mesh, uvs, last_texture);

        const Json::Value draco_options = config["draco"];
        const Json::Value network = config["network"];
        const bool network_enabled = network.get("enabled", false).asBool();
        const std::string network_format = network.get("format", "raw").asString();
        const bool draco_enabled = draco_options.get("enabled", false).asBool() ||
                                   (network_enabled && network_format == "draco");
        std::vector<uint8_t> draco_data;
        if (draco_enabled) {
            draco_data = EncodeDraco(mesh, uvs, draco_options);
            const std::string draco_path = prefix + ".drc";
            std::ofstream draco_output(draco_path, std::ios::binary);
            draco_output.write(reinterpret_cast<const char *>(draco_data.data()),
                               static_cast<std::streamsize>(draco_data.size()));
            if (!draco_output) {
                throw std::runtime_error("failed to write " + draco_path);
            }
            std::cout << "wrote " << draco_path << " bytes=" << draco_data.size()
                      << '\n';
        }
        std::cout << "mesh faces before_qem=" << original_faces
                  << " after_qem=" << mesh.triangles_.size()
                  << " vertices=" << mesh.vertices_.size() << '\n';
        std::cout << "wrote " << prefix << ".ply, .obj, .mtl, and _texture.png\n";

        if (network_enabled) {
            if (network_format == "draco") {
                SendDracoFrame(network, draco_data, last_texture);
            } else if (network_format == "raw") {
                SendRawFrame(network, mesh, uvs, last_texture);
            } else {
                throw std::runtime_error("network.format must be raw or draco");
            }
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
