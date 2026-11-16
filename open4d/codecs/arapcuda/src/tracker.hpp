// Internal C++ tracker behind the Python module (python/bindings.cpp).
//
// Places a fixed number of centers inside the volume of the first mesh and
// follows them through the sequence: each frame balances keeping the centers
// evenly spread over the volume (a Lloyd step) against moving neighbouring
// centers as rigidly as possible (an ARAP step). This is the tracker of TSMC
// (Dvorak et al.), with the whole loop running on the GPU.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace arapcuda {

struct Params
{
    int centers = 2000;                // number of tracked centers
    int grid_resolution = 512;         // voxels along the longest side of each mesh's bounding box
    float gradient_threshold = 1e-4f;  // a frame is done once no center moves farther than this per iteration
    float smooth_sigma = 0.125f;       // distance at which spatial affinity falls to 1/2
    float smooth_sigma2 = 0.125f;      // transform difference at which affinity falls to 1/2
    float arap_cutoff = 0.001f;        // neighbours with a lower affinity are ignored
    float apply_lloyd = 1.0f;          // weight of the even-spread (Lloyd) term
    float apply_smooth = 1.0f;         // weight of the rigidity (ARAP) term
    int presmooth_iterations = 10;     // ARAP-only steps before each frame's optimization
    int max_iterations = 1000;         // cap on optimization iterations per frame
    int init_iterations = 100;         // Lloyd iterations that place the first frame's centers
    int seed = 0;                      // seed of the initial random placement
    int device = 0;                    // CUDA device index
};

// Borrowed mesh data. vertices: vertex_count x 3 floats; triangles: triangle_count x 3 indices.
struct MeshView
{
    const float* vertices;
    int vertex_count;
    const int32_t* triangles;
    int triangle_count;
};

struct Frame
{
    std::vector<float> centers;     // centers x 3 (x, y, z)
    std::vector<float> transforms;  // centers x 12: R (3 x 3, row major), t (3); maps each center's
                                    // neighbourhood from the previous frame to this one. Empty for frame 0.
    int iterations = 0;             // optimization iterations used (Lloyd iterations for frame 0)
    double seconds = 0;             // wall time of add_frame
};

class Tracker
{
public:
    explicit Tracker(const Params& params = Params());
    ~Tracker();
    Tracker(Tracker&&) noexcept;
    Tracker& operator=(Tracker&&) noexcept;
    Tracker(const Tracker&) = delete;
    Tracker& operator=(const Tracker&) = delete;

    // Tracks the next frame of the sequence. Throws std::runtime_error on failure.
    Frame add_frame(const MeshView& mesh);

    int frame_count() const;
    const Params& params() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Number of visible CUDA devices (0 if there is no usable driver).
int device_count();

} // namespace arapcuda
