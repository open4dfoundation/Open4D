// Python bindings (module arapcuda._core); the public API is in arapcuda/__init__.py.

#include "tracker.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace arapcuda;

namespace {

using FloatArray = py::array_t<float, py::array::c_style | py::array::forcecast>;
using IndexArray = py::array_t<int32_t, py::array::c_style | py::array::forcecast>;

py::object addFrame(Tracker& tracker, FloatArray vertices, IndexArray triangles)
{
    if (vertices.ndim() != 2 || vertices.shape(1) != 3)
        throw py::value_error("vertices must have shape (V, 3)");
    if (triangles.ndim() != 2 || triangles.shape(1) != 3)
        throw py::value_error("triangles must have shape (T, 3)");
    MeshView mesh{ vertices.data(), (int)vertices.shape(0), triangles.data(), (int)triangles.shape(0) };

    Frame f;
    {
        py::gil_scoped_release release;
        f = tracker.add_frame(mesh);
    }

    const py::ssize_t n = tracker.params().centers;
    FloatArray centers({ n, (py::ssize_t)3 });
    std::copy(f.centers.begin(), f.centers.end(), centers.mutable_data());

    py::object transforms = py::none();
    if (!f.transforms.empty())
    {
        FloatArray t({ n, (py::ssize_t)3, (py::ssize_t)4 });   // [R | t]
        float* out = t.mutable_data();
        for (py::ssize_t i = 0; i < n; i++)
        {
            const float* src = &f.transforms[12 * i];
            for (int r = 0; r < 3; r++)
            {
                for (int c = 0; c < 3; c++)
                    out[12 * i + 4 * r + c] = src[3 * r + c];
                out[12 * i + 4 * r + 3] = src[9 + r];
            }
        }
        transforms = t;
    }

    py::dict result;
    result["centers"] = centers;
    result["transforms"] = transforms;
    result["iterations"] = f.iterations;
    result["seconds"] = f.seconds;
    return result;
}

} // namespace

PYBIND11_MODULE(_core, m)
{
    py::class_<Params>(m, "Params")
        .def(py::init<>())
        .def_readwrite("centers", &Params::centers)
        .def_readwrite("grid_resolution", &Params::grid_resolution)
        .def_readwrite("gradient_threshold", &Params::gradient_threshold)
        .def_readwrite("smooth_sigma", &Params::smooth_sigma)
        .def_readwrite("smooth_sigma2", &Params::smooth_sigma2)
        .def_readwrite("arap_cutoff", &Params::arap_cutoff)
        .def_readwrite("apply_lloyd", &Params::apply_lloyd)
        .def_readwrite("apply_smooth", &Params::apply_smooth)
        .def_readwrite("presmooth_iterations", &Params::presmooth_iterations)
        .def_readwrite("max_iterations", &Params::max_iterations)
        .def_readwrite("init_iterations", &Params::init_iterations)
        .def_readwrite("seed", &Params::seed)
        .def_readwrite("device", &Params::device);

    py::class_<Tracker>(m, "Tracker")
        .def(py::init<const Params&>())
        .def("add_frame", &addFrame, py::arg("vertices"), py::arg("triangles"))
        .def_property_readonly("frame_count", &Tracker::frame_count);

    m.def("device_count", &device_count);
}
