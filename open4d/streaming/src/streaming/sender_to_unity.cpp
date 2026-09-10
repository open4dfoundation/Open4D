#include "sender_to_unity.hpp"

#include <thread>

#include "data_type.hpp"
#include "network_stream.hpp"

SenderToUnity::SenderToUnity(
    NetworkStream&& ns, PipeDataIn<MeshFrame>* const src)
  : ns_{std::move(ns)}, src_(src)
{
  this->thread_ = std::thread(&SenderToUnity::fetch, this);
}

void SenderToUnity::fetch()
{
  try
  {
    while (1)
    {
      std::cout << "sending to unity" << std::endl;
      MeshFrame frame = this->src_->fetch();

      std::vector<Mesh> meshes = {frame.mesh};
      std::cout << meshes.at(0).ToString() << std::endl;
      sendMeshes(meshes);
      sendTexture(&(frame.texture));
    }
  }
  catch (const InTerminatedException&)
  {
    // Handle the termination appropriately
  }
}

using Vector3 = std::array<float, 3>;
using Vector2 = std::array<float, 2>;

struct VertexData
{
  Vector3 pos;
  Vector2 uv;
} __attribute__((packed));

void SenderToUnity::sendMeshes(const std::vector<Mesh>& meshes)
{
  uint32_t totNF = 0;
  for (const auto& mesh : meshes)
    totNF += mesh.GetTriangleIndices().GetLength();
  this->ns_.sendAll(&totNF, sizeof(totNF));
  for (const auto& mesh : meshes)
  {
    auto& vertexPositions = mesh.GetVertexPositions();
    auto triangleIndices
        = mesh.GetTriangleIndices().To(open3d::core::Dtype::Int32);
    auto& textureUVs = mesh.GetTriangleAttr("texture_uvs");

    uint32_t nF = static_cast<uint32_t>(triangleIndices.GetLength());

    std::vector<VertexData> vertexData(nF * 3);
    auto* pPositions
        = reinterpret_cast<const Vector3*>(vertexPositions.GetDataPtr());
    auto* pUV = reinterpret_cast<const Vector2*>(textureUVs.GetDataPtr());
    auto* pIndices
        = reinterpret_cast<const uint32_t*>(triangleIndices.GetDataPtr());

    for (uint32_t i = 0; i < nF * 3; i++)
    {
      vertexData[i] = {pPositions[pIndices[i]], pUV[i]};
    }
    this->ns_.sendAll(
        vertexData.data(), vertexData.size() * sizeof(VertexData));
  }
}

void SenderToUnity::sendTexture(const cv::Mat* texture)
{
  this->ns_.sendAll(texture->data, texture->cols * texture->rows * 3);
}
