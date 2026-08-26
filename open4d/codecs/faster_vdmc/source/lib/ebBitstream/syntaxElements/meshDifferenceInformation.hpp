#pragma once
#include <vector>
namespace eb {
class MeshDifferenceInformation {
public:
  MeshDifferenceInformation() {}
  ~MeshDifferenceInformation() {}
  MeshDifferenceInformation&
  operator=(const MeshDifferenceInformation&) = default;

  auto& getMeshAddPointCount() const { return meshAddPointCount_; }
  auto& getMeshDeletePointCount() const { return meshDeletePointCount_; }
  auto& getModifyMeshPointCount() const { return modifyMeshPointCount_; }

  auto& getAddPointIdxDelta() const { return meshAddPointListDelta_; }
  auto& getDeletePointIdxDelta() const { return meshDeletePointListDelta_; }
  auto& getModifyTrianglesDelta() const { return modifyTrianglesDelta_; }
  auto& getModifyIdx() const { return modifyIdx_; }
  auto& getModifyPointIdxDelta() const { return modifyPointIdxDelta_; }

  auto& getMeshAddPointCount() { return meshAddPointCount_; }
  auto& getMeshDeletePointCount() { return meshDeletePointCount_; }
  auto& getModifyMeshPointCount() { return modifyMeshPointCount_; }

  auto& getAddPointIdxDelta() { return meshAddPointListDelta_; }
  auto& getDeletePointIdxDelta() { return meshDeletePointListDelta_; }
  auto& getModifyTrianglesDelta() { return modifyTrianglesDelta_; }
  auto& getModifyIdx() { return modifyIdx_; }
  auto& getModifyPointIdxDelta() { return modifyPointIdxDelta_; }

private:
  uint32_t meshAddPointCount_ = 0;
  uint32_t meshDeletePointCount_ = 0;
  uint32_t modifyMeshPointCount_ = 0;
  std::vector<int32_t> meshAddPointListDelta_;
  std::vector<int32_t> meshDeletePointListDelta_;
  std::vector<int32_t> modifyTrianglesDelta_;
  std::vector<uint32_t> modifyIdx_;
  std::vector<int32_t> modifyPointIdxDelta_;
};
}  // namespace eb