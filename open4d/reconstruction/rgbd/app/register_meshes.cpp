#include <open3d/Open3D.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using open3d::geometry::KDTreeSearchParamHybrid;
using open3d::geometry::PointCloud;
using open3d::geometry::TriangleMesh;
using namespace open3d::pipelines::registration;

std::shared_ptr<PointCloud> Prepare(const TriangleMesh &mesh, double voxel_size) {
    auto points = std::make_shared<PointCloud>(mesh.vertices_);
    points = points->VoxelDownSample(voxel_size);
    points->EstimateNormals(KDTreeSearchParamHybrid(voxel_size * 2.0, 40));
    return points;
}

RegistrationResult Refine(const PointCloud &source,
                          const PointCloud &target,
                          const Eigen::Matrix4d &initial,
                          double distance) {
    return RegistrationICP(
        source, target, distance, initial,
        TransformationEstimationPointToPlane(),
        ICPConvergenceCriteria(1e-7, 1e-7, 100));
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: " << argv[0]
                      << " <source-j3.ply> <target-ey.ply> <output.ply>\n";
            return 2;
        }
        auto source_mesh = open3d::io::CreateMeshFromFile(argv[1]);
        auto target_mesh = open3d::io::CreateMeshFromFile(argv[2]);
        if (!source_mesh || source_mesh->IsEmpty() ||
            !target_mesh || target_mesh->IsEmpty()) {
            throw std::runtime_error("failed to load both input meshes");
        }

        constexpr double voxel = 0.04;
        auto source = Prepare(*source_mesh, voxel);
        auto target = Prepare(*target_mesh, voxel);
        if (source->points_.size() < 100 || target->points_.size() < 100) {
            throw std::runtime_error("not enough downsampled points for registration");
        }
        auto source_fpfh = ComputeFPFHFeature(
            *source, KDTreeSearchParamHybrid(voxel * 5.0, 100));
        auto target_fpfh = ComputeFPFHFeature(
            *target, KDTreeSearchParamHybrid(voxel * 5.0, 100));

        const auto global = FastGlobalRegistrationBasedOnFeatureMatching(
            *source, *target, *source_fpfh, *target_fpfh,
            FastGlobalRegistrationOption(1.4, true, true, voxel * 2.5,
                                         128, 0.9, 2000, true));
        const auto fgr_icp = Refine(*source, *target, global.transformation_,
                                    voxel * 1.5);
        const auto identity_icp = Refine(*source, *target,
                                         Eigen::Matrix4d::Identity(),
                                         voxel * 1.5);
        const RegistrationResult &best =
            identity_icp.fitness_ > fgr_icp.fitness_ ? identity_icp : fgr_icp;

        std::cout << std::fixed << std::setprecision(6)
                  << "source_points=" << source->points_.size()
                  << " target_points=" << target->points_.size() << '\n'
                  << "fgr_fitness=" << global.fitness_
                  << " fgr_rmse=" << global.inlier_rmse_ << '\n'
                  << "fgr_icp_fitness=" << fgr_icp.fitness_
                  << " fgr_icp_rmse=" << fgr_icp.inlier_rmse_ << '\n'
                  << "identity_icp_fitness=" << identity_icp.fitness_
                  << " identity_icp_rmse=" << identity_icp.inlier_rmse_ << '\n'
                  << "selected_transform_j3_to_ey:\n"
                  << best.transformation_ << '\n';

        if (best.fitness_ < 0.20 || best.inlier_rmse_ > voxel) {
            throw std::runtime_error(
                "registration rejected: insufficient reliable overlap");
        }

        source_mesh->Transform(best.transformation_);
        TriangleMesh combined = *target_mesh;
        combined += *source_mesh;
        combined.MergeCloseVertices(0.005);
        combined.RemoveDuplicatedVertices();
        combined.RemoveDuplicatedTriangles();
        combined.RemoveDegenerateTriangles();
        combined.RemoveUnreferencedVertices();
        combined.ComputeVertexNormals();
        if (!open3d::io::WriteTriangleMesh(argv[3], combined, true, false, true)) {
            throw std::runtime_error("failed to write registered mesh");
        }
        std::cout << "wrote=" << argv[3]
                  << " vertices=" << combined.vertices_.size()
                  << " triangles=" << combined.triangles_.size() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
