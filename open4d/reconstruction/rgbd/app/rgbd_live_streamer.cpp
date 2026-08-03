#include <k4a/k4a.hpp>
#include <jsoncpp/json/json.h>
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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kMrd3Magic = 0x4d524433;
constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kMessageFrame = 1;
constexpr uint32_t kGeometryDraco = 1;
constexpr uint32_t kImageJpeg = 1;

std::atomic<bool> g_stop{false};

void HandleSignal(int) {
    g_stop = true;
}

struct CapturedFrame {
    uint64_t frame_id = 0;
    uint64_t timestamp_usec = 0;
    k4a::image color;
    k4a::image depth;
};

class LatestFrameQueue {
public:
    void Put(CapturedFrame frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        if (frame_.has_value()) ++dropped_;
        frame_ = std::move(frame);
        readable_.notify_one();
    }

    bool Get(CapturedFrame &frame, uint64_t &dropped) {
        std::unique_lock<std::mutex> lock(mutex_);
        readable_.wait(lock, [&] {
            return frame_.has_value() || closed_ || g_stop.load();
        });
        if (!frame_.has_value()) return false;
        frame = std::move(*frame_);
        frame_.reset();
        dropped = dropped_;
        dropped_ = 0;
        return true;
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        readable_.notify_all();
    }

private:
    std::optional<CapturedFrame> frame_;
    uint64_t dropped_ = 0;
    bool closed_ = false;
    std::mutex mutex_;
    std::condition_variable readable_;
};

struct LiveMesh {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<uint32_t, 3>> faces;
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
    const std::string wanted = config.get("serial", "").asString();
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

bool Continuous(uint16_t a, uint16_t b, uint16_t c, uint16_t threshold_mm) {
    const uint16_t minimum = std::min({a, b, c});
    const uint16_t maximum = std::max({a, b, c});
    return minimum > 0 && maximum - minimum <= threshold_mm;
}

LiveMesh BuildDepthMesh(const k4a::image &depth,
                        const k4a::calibration &calibration,
                        int color_width,
                        int color_height,
                        int stride,
                        uint16_t edge_threshold_mm,
                        uint16_t min_depth_mm,
                        uint16_t max_depth_mm) {
    const int width = depth.get_width_pixels();
    const int height = depth.get_height_pixels();
    const int columns = (width + stride - 1) / stride;
    const int rows = (height + stride - 1) / stride;
    const auto &p =
        calibration.depth_camera_calibration.intrinsics.parameters.param;

    LiveMesh mesh;
    mesh.positions.reserve(static_cast<size_t>(columns) * rows);
    mesh.uvs.reserve(static_cast<size_t>(columns) * rows);
    std::vector<int32_t> vertex_index(static_cast<size_t>(columns) * rows, -1);
    std::vector<uint16_t> samples(static_cast<size_t>(columns) * rows, 0);

    for (int gy = 0, v = 0; v < height; ++gy, v += stride) {
        const auto *row = reinterpret_cast<const uint16_t *>(
            depth.get_buffer() + static_cast<size_t>(v) * depth.get_stride_bytes());
        for (int gx = 0, u = 0; u < width; ++gx, u += stride) {
            const size_t grid_index = static_cast<size_t>(gy) * columns + gx;
            const uint16_t depth_mm = row[u];
            samples[grid_index] = depth_mm;
            if (depth_mm < min_depth_mm || depth_mm > max_depth_mm) continue;
            const float z = static_cast<float>(depth_mm) * 0.001f;
            const float x = (static_cast<float>(u) - p.cx) * z / p.fx;
            const float y = (static_cast<float>(v) - p.cy) * z / p.fy;
            k4a_float2_t depth_pixel{};
            depth_pixel.xy.x = static_cast<float>(u);
            depth_pixel.xy.y = static_cast<float>(v);
            k4a_float2_t color_pixel{};
            if (!calibration.convert_2d_to_2d(
                    depth_pixel, static_cast<float>(depth_mm),
                    K4A_CALIBRATION_TYPE_DEPTH, K4A_CALIBRATION_TYPE_COLOR,
                    &color_pixel)) {
                continue;
            }
            vertex_index[grid_index] =
                static_cast<int32_t>(mesh.positions.size());
            mesh.positions.push_back({x, y, z});
            mesh.uvs.push_back({
                std::clamp(color_pixel.xy.x / std::max(1, color_width - 1),
                           0.0f, 1.0f),
                1.0f -
                    std::clamp(color_pixel.xy.y /
                                   std::max(1, color_height - 1),
                               0.0f, 1.0f)});
        }
    }

    auto add_triangle = [&](size_t a, size_t b, size_t c) {
        if (vertex_index[a] < 0 || vertex_index[b] < 0 ||
            vertex_index[c] < 0) {
            return;
        }
        if (!Continuous(samples[a], samples[b], samples[c],
                        edge_threshold_mm)) {
            return;
        }
        mesh.faces.push_back({
            static_cast<uint32_t>(vertex_index[a]),
            static_cast<uint32_t>(vertex_index[b]),
            static_cast<uint32_t>(vertex_index[c])});
    };

    for (int y = 0; y + 1 < rows; ++y) {
        for (int x = 0; x + 1 < columns; ++x) {
            const size_t a = static_cast<size_t>(y) * columns + x;
            const size_t b = a + 1;
            const size_t c = a + columns;
            const size_t d = c + 1;
            add_triangle(a, c, b);
            add_triangle(b, c, d);
        }
    }
    return mesh;
}

std::vector<uint8_t> EncodeDraco(const LiveMesh &mesh,
                                 const Json::Value &options) {
    draco::Mesh encoded;
    encoded.set_num_points(static_cast<uint32_t>(mesh.positions.size()));

    draco::GeometryAttribute definition;
    definition.Init(draco::GeometryAttribute::POSITION, nullptr, 3,
                    draco::DT_FLOAT32, false, sizeof(float) * 3, 0);
    const int position_id = encoded.AddAttribute(
        definition, true, static_cast<uint32_t>(mesh.positions.size()));
    definition.Init(draco::GeometryAttribute::TEX_COORD, nullptr, 2,
                    draco::DT_FLOAT32, false, sizeof(float) * 2, 0);
    const int uv_id = encoded.AddAttribute(
        definition, true, static_cast<uint32_t>(mesh.uvs.size()));
    if (position_id < 0 || uv_id < 0) {
        throw std::runtime_error("failed to create Draco attributes");
    }

    auto *positions = encoded.attribute(position_id);
    auto *uvs = encoded.attribute(uv_id);
    for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
        positions->SetAttributeValue(draco::AttributeValueIndex(i),
                                     mesh.positions[i].data());
        uvs->SetAttributeValue(draco::AttributeValueIndex(i),
                               mesh.uvs[i].data());
    }
    for (const auto &source_face : mesh.faces) {
        draco::Mesh::Face face;
        face[0] = draco::PointIndex(source_face[0]);
        face[1] = draco::PointIndex(source_face[1]);
        face[2] = draco::PointIndex(source_face[2]);
        encoded.AddFace(face);
    }

    draco::Encoder encoder;
    encoder.SetSpeedOptions(options.get("encoding_speed", 8).asInt(),
                            options.get("decoding_speed", 8).asInt());
    encoder.SetAttributeQuantization(
        draco::GeometryAttribute::POSITION,
        options.get("position_quantization_bits", 11).asInt());
    encoder.SetAttributeQuantization(
        draco::GeometryAttribute::TEX_COORD,
        options.get("texcoord_quantization_bits", 10).asInt());
    draco::EncoderBuffer buffer;
    const draco::Status status = encoder.EncodeMeshToBuffer(encoded, &buffer);
    if (!status.ok()) {
        throw std::runtime_error("Draco encode failed: " +
                                 status.error_msg_string());
    }
    return std::vector<uint8_t>(buffer.data(), buffer.data() + buffer.size());
}

std::vector<uint8_t> EncodeJpeg(const k4a::image &color, int quality) {
    cv::Mat bgra(color.get_height_pixels(), color.get_width_pixels(), CV_8UC4,
                 const_cast<uint8_t *>(color.get_buffer()),
                 color.get_stride_bytes());
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    std::vector<uint8_t> encoded;
    if (!cv::imencode(".jpg", bgr, encoded,
                      {cv::IMWRITE_JPEG_QUALITY, quality})) {
        throw std::runtime_error("JPEG encode failed");
    }
    return encoded;
}

int CreateListener(const std::string &address, uint16_t port) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) throw std::runtime_error("socket failed");
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<sockaddr *>(&endpoint),
             sizeof(endpoint)) != 0 ||
        listen(listener, 4) != 0) {
        close(listener);
        throw std::runtime_error("failed to listen on " + address + ":" +
                                 std::to_string(port));
    }
    return listener;
}

bool SendAll(int socket_fd, const void *data, size_t bytes) {
    const auto *cursor = static_cast<const uint8_t *>(data);
    while (bytes > 0) {
        const ssize_t sent = send(socket_fd, cursor, bytes, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        cursor += sent;
        bytes -= static_cast<size_t>(sent);
    }
    return true;
}

bool SendFrame(int client,
               const CapturedFrame &frame,
               uint64_t dropped,
               const LiveMesh &mesh,
               const std::vector<uint8_t> &geometry,
               const std::vector<uint8_t> &image) {
    const uint32_t flags = (kGeometryDraco << 16) | kImageJpeg;
    uint32_t header[14] = {
        kMrd3Magic,
        kProtocolVersion,
        kMessageFrame,
        static_cast<uint32_t>(frame.frame_id),
        static_cast<uint32_t>(frame.timestamp_usec >> 32),
        static_cast<uint32_t>(frame.timestamp_usec),
        static_cast<uint32_t>(mesh.positions.size()),
        static_cast<uint32_t>(mesh.faces.size()),
        static_cast<uint32_t>(geometry.size()),
        static_cast<uint32_t>(image.size()),
        static_cast<uint32_t>(frame.color.get_width_pixels()),
        static_cast<uint32_t>(frame.color.get_height_pixels()),
        static_cast<uint32_t>(std::min<uint64_t>(dropped, UINT32_MAX)),
        flags};
    for (uint32_t &value : header) value = htonl(value);
    return SendAll(client, header, sizeof(header)) &&
           SendAll(client, geometry.data(), geometry.size()) &&
           SendAll(client, image.data(), image.size());
}

}  // namespace

int main(int argc, char **argv) {
    std::thread producer;
    try {
        if (argc != 2) {
            std::cerr << "usage: " << argv[0] << " <config.live.json>\n";
            return 2;
        }
        Json::Value config;
        std::ifstream input(argv[1]);
        if (!(input >> config)) throw std::runtime_error("invalid config JSON");

        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);
        k4a::depth_engine_helper::create();

        const Json::Value capture = config["capture"];
        k4a_device_configuration_t camera_config =
            K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
        camera_config.camera_fps = ParseFps(capture.get("fps", 15).asInt());
        camera_config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
        camera_config.color_resolution = ParseColorResolution(
            capture.get("color_resolution", "720p").asString());
        camera_config.depth_mode = ParseDepthMode(
            capture.get("depth_mode", "nfov_2x2binned").asString());
        camera_config.synchronized_images_only = true;
        camera_config.wired_sync_mode = K4A_WIRED_SYNC_MODE_STANDALONE;

        const uint32_t device_index = ResolveDevice(config["device"]);
        auto device = k4a::device::open(device_index);
        const std::string serial = device.get_serialnum();
        const auto calibration = device.get_calibration(
            camera_config.depth_mode, camera_config.color_resolution);
        device.start_cameras(&camera_config);

        LatestFrameQueue queue;
        std::exception_ptr producer_error;
        const int timeout_ms = capture.get("timeout_ms", 2000).asInt();
        const uint64_t max_frames = capture.get("max_frames", 0).asUInt64();
        producer = std::thread([&] {
            try {
                uint64_t frame_id = 0;
                while (!g_stop && (max_frames == 0 || frame_id < max_frames)) {
                    k4a::capture captured;
                    if (!device.get_capture(
                            &captured, std::chrono::milliseconds(timeout_ms))) {
                        continue;
                    }
                    k4a::image color = captured.get_color_image();
                    k4a::image depth = captured.get_depth_image();
                    if (!color.is_valid() || !depth.is_valid()) continue;
                    const uint64_t timestamp_usec =
                        static_cast<uint64_t>(
                            depth.get_device_timestamp().count());
                    queue.Put({frame_id++, timestamp_usec, std::move(color),
                               std::move(depth)});
                }
            } catch (...) {
                producer_error = std::current_exception();
            }
            queue.Close();
        });

        const Json::Value network = config["network"];
        const std::string address =
            network.get("bind_address", "0.0.0.0").asString();
        const uint16_t port =
            static_cast<uint16_t>(network.get("port", 33669).asUInt());
        const int listener = CreateListener(address, port);
        std::cout << "live camera serial=" << serial << " listening=" << address
                  << ':' << port << '\n';

        const Json::Value geometry_options = config["geometry"];
        const int stride = std::max(1, geometry_options.get("stride", 4).asInt());
        const uint16_t edge_threshold_mm = static_cast<uint16_t>(
            geometry_options.get("edge_threshold_mm", 50).asUInt());
        const uint16_t min_depth_mm = static_cast<uint16_t>(
            geometry_options.get("min_depth_mm", 200).asUInt());
        const uint16_t max_depth_mm = static_cast<uint16_t>(
            geometry_options.get("max_depth_mm", 3000).asUInt());
        const int jpeg_quality = std::clamp(
            config["image"].get("jpeg_quality", 75).asInt(), 1, 100);

        int client = -1;
        uint64_t sent_frames = 0;
        CapturedFrame frame;
        uint64_t dropped = 0;
        while (!g_stop && queue.Get(frame, dropped)) {
            if (client < 0) {
                std::cout << "waiting for MRD3 client\n";
                client = accept(listener, nullptr, nullptr);
                if (client < 0) {
                    if (g_stop) break;
                    continue;
                }
                std::cout << "MRD3 client connected\n";
            }

            const auto started = std::chrono::steady_clock::now();
            LiveMesh mesh = BuildDepthMesh(
                frame.depth, calibration, frame.color.get_width_pixels(),
                frame.color.get_height_pixels(), stride, edge_threshold_mm,
                min_depth_mm, max_depth_mm);
            std::vector<uint8_t> geometry =
                EncodeDraco(mesh, config["draco"]);
            std::vector<uint8_t> image = EncodeJpeg(frame.color, jpeg_quality);
            const auto encoded = std::chrono::steady_clock::now();
            if (!SendFrame(client, frame, dropped, mesh, geometry, image)) {
                close(client);
                client = -1;
                std::cerr << "client disconnected; newest frame retained\n";
                continue;
            }
            const auto sent = std::chrono::steady_clock::now();
            const double encode_ms =
                std::chrono::duration<double, std::milli>(encoded - started)
                    .count();
            const double send_ms =
                std::chrono::duration<double, std::milli>(sent - encoded)
                    .count();
            std::cout << "frame=" << frame.frame_id
                      << " vertices=" << mesh.positions.size()
                      << " faces=" << mesh.faces.size()
                      << " draco_bytes=" << geometry.size()
                      << " jpeg_bytes=" << image.size()
                      << " dropped=" << dropped
                      << " encode_ms=" << encode_ms
                      << " send_ms=" << send_ms << '\n';
            ++sent_frames;
        }

        if (client >= 0) close(client);
        close(listener);
        g_stop = true;
        queue.Close();
        if (producer.joinable()) producer.join();
        device.stop_cameras();
        if (producer_error) std::rethrow_exception(producer_error);
        std::cout << "stream stopped sent_frames=" << sent_frames << '\n';
        return 0;
    } catch (const std::exception &error) {
        g_stop = true;
        if (producer.joinable()) producer.join();
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
