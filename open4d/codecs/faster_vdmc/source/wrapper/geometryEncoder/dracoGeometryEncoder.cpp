/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#if defined(USE_DRACO_GEOMETRY_CODEC)
#  include "util/mesh.hpp"
#  include "dracoGeometryEncoder.hpp"
#  include <set>
#  include <memory>

#  include "draco/compression/encode.h"
#  include "draco/compression/decode.h"
#  include "draco/core/data_buffer.h"
#  include "draco/mesh/triangle_soup_mesh_builder.h"
#  include "draco/io/obj_encoder.h"

namespace vmesh {

template<typename T>
DracoGeometryEncoder<T>::DracoGeometryEncoder() = default;
template<typename T>
DracoGeometryEncoder<T>::~DracoGeometryEncoder() = default;

template<typename T>
std::unique_ptr<draco::Mesh>
convert(TriangleMesh<T>& src, const bool deduplicateAttribute) {
  const bool    enableNormal  = false;
  const int32_t triCount      = src.triangleCount();
  const int32_t pointCount    = src.pointCount();
  const int32_t texCoordCount = src.texCoordCount();
  const int32_t normalCount   = src.normalCount();
  const int32_t colourCount   = src.colourCount();
  const int32_t faceIdCount   = src.faceIdCount();
  int           posAtt        = -1;
  int           nrmAtt        = -1;
  int           colAtt        = -1;
  int           texAtt        = -1;
  int           fidAtt        = -1;

  // Add attributes if they are present in the input data.
  const bool use_identity_mapping = false;
  auto       mesh                 = std::make_unique<draco::Mesh>();
  mesh->SetNumFaces(triCount);
  mesh->set_num_points(3 * triCount);

  draco::PointAttribute va;
  va.Init(draco::GeometryAttribute::POSITION, 3, draco::DT_INT32, false, 0);
  posAtt = mesh->AddAttribute(va, use_identity_mapping, pointCount);

  if (enableNormal && normalCount > 0) {
    draco::GeometryAttribute va;
    va.Init(draco::GeometryAttribute::NORMAL,
            nullptr,
            3,
            draco::DT_INT32,
            false,
            sizeof(int32_t) * 3,
            0);
    nrmAtt = mesh->AddAttribute(va, use_identity_mapping, normalCount);
  }
  if (colourCount > 0) {
    draco::GeometryAttribute va;
    va.Init(draco::GeometryAttribute::NORMAL,
            nullptr,
            3,
            draco::DT_INT32,
            false,
            sizeof(int32_t) * 3,
            0);
    colAtt = mesh->AddAttribute(va, use_identity_mapping, colourCount);
  }
  if (texCoordCount > 0) {
    draco::GeometryAttribute va;
    va.Init(draco::GeometryAttribute::TEX_COORD,
            nullptr,
            2,
            draco::DT_INT32,
            false,
            sizeof(int32_t) * 2,
            0);
    texAtt = mesh->AddAttribute(va, use_identity_mapping, texCoordCount);
  }
  if (faceIdCount > 0) {
    draco::GeometryAttribute va;
    va.Init(draco::GeometryAttribute::GENERIC,
            nullptr,
            1,
            draco::DT_INT32,
            false,
            sizeof(int32_t),
            0);
    auto maxFaceId =
      *max_element(std::begin(src.faceIds()), std::end(src.faceIds()));
    fidAtt = mesh->AddAttribute(va, use_identity_mapping, maxFaceId + 1);
  }

  draco::AttributeValueIndex posIndex(0);
  draco::AttributeValueIndex colIndex(0);
  draco::AttributeValueIndex nrmIndex(0);
  draco::AttributeValueIndex texIndex(0);
  draco::AttributeValueIndex fidIndex(0);
  for (int32_t i = 0; i < pointCount; i++) {
    Vec3<int32_t> pos(src.point(i));
    mesh->attribute(posAtt)->SetAttributeValue(posIndex++, pos.data());
  }
  if (enableNormal && normalCount > 0) {
    for (int32_t i = 0; i < normalCount; i++) {
      Vec3<int32_t> nrm(src.normal(i));
      mesh->attribute(nrmAtt)->SetAttributeValue(nrmIndex++, nrm.data());
    }
  }
  if (colourCount > 0) {
    for (int32_t i = 0; i < colourCount; i++) {
      Vec3<int32_t> col(src.colour(i));
      mesh->attribute(colAtt)->SetAttributeValue(colIndex++, col.data());
    }
  }
  if (texCoordCount > 0) {
    for (int32_t i = 0; i < texCoordCount; i++) {
      Vec2<int32_t> tex(src.texCoord(i));
      mesh->attribute(texAtt)->SetAttributeValue(texIndex++, tex.data());
    }
  }
  if (faceIdCount > 0) {
    auto maxFaceId =
      *max_element(std::begin(src.faceIds()), std::end(src.faceIds()));
    for (int32_t i = 0; i < maxFaceId + 1; i++) {
      mesh->attribute(fidAtt)->SetAttributeValue(fidIndex++, &i);
    }
  }
  for (int32_t i = 0; i < triCount; i++) {
    draco::Mesh::Face face;
    Vec3<int32_t>     tri(src.triangle(i));
    for (int c = 0; c < 3; ++c) {
      face[c] = 3 * i + c;
      mesh->attribute(posAtt)->SetPointMapEntry(
        face[c], draco::AttributeValueIndex(tri[c]));
    }
    if (texCoordCount > 0) {
      Vec3<int32_t> tex(src.texCoordTriangle(i));
      for (int c = 0; c < 3; ++c) {
        mesh->attribute(texAtt)->SetPointMapEntry(
          face[c], draco::AttributeValueIndex(tex[c]));
      }
    }
    if (enableNormal && normalCount > 0 && src.normalTriangles().size()) {
      Vec3<int32_t> nrm(src.normalTriangle(i));
      for (int c = 0; c < 3; ++c) {
        mesh->attribute(nrmAtt)->SetPointMapEntry(
          face[c], draco::AttributeValueIndex(nrm[c]));
      }
    }
    if (faceIdCount > 0) {
      auto faceId = src.faceId(i);
      for (int c = 0; c < 3; ++c) {
        mesh->attribute(fidAtt)->SetPointMapEntry(
          face[c], draco::AttributeValueIndex(faceId));
      }
    }
    mesh->SetFace(draco::FaceIndex(i), face);
  }
  if (deduplicateAttribute) mesh->DeduplicateAttributeValues();
  mesh->DeduplicatePointIds();
  return mesh;
}

template<typename T>
void
convert(std::unique_ptr<draco::Mesh>& src,
        TriangleMesh<T>&              dst,
        bool                          mesh_lossless) {
  draco::Mesh& mesh = *src;
  const auto*  posAtt =
    mesh.GetNamedAttribute(draco::GeometryAttribute::POSITION);
  const auto* nrmAtt =
    mesh.GetNamedAttribute(draco::GeometryAttribute::NORMAL);
  const auto* colAtt = mesh.GetNamedAttribute(draco::GeometryAttribute::COLOR);
  const auto* texAtt =
    mesh.GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
  const auto* fidAtt =
    mesh.GetNamedAttribute(draco::GeometryAttribute::GENERIC);
  // position
  if (posAtt) {
    if (!mesh_lossless) {
      for (draco::AttributeValueIndex i(0);
           i < static_cast<uint32_t>(posAtt->size());
           ++i) {
        std::array<int32_t, 3> value{};
        if (!posAtt->ConvertValue<int32_t, 3>(i, value.data())) { return; }
        dst.addPoint(value[0], value[1], value[2]);
      }
    } else {
      typedef std::array<float, 3> AttributeHashableValue;
      std::unordered_map<AttributeHashableValue,
                         std::set<int>,
                         draco::HashArray<AttributeHashableValue>>
        pos_value_ids_maps;
      for (draco::AttributeValueIndex i(0);
           i < static_cast<uint32_t>(posAtt->size());
           ++i) {
        std::array<float, 3> value{};
        if (!posAtt->ConvertValue<float, 3>(i, value.data())) { return; }
        pos_value_ids_maps[value].insert(i.value());
        dst.addPoint(value[0], value[1], value[2]);
      }
      if (mesh_lossless && texAtt) {
        std::unordered_map<AttributeHashableValue,
                           std::set<int>,
                           draco::HashArray<AttributeHashableValue>>
          pos_tex_maps;

        for (draco::FaceIndex i(0); i < mesh.num_faces(); ++i) {
          const draco::Mesh::Face& face = mesh.face(i);
          for (int j = 0; j < 3; ++j) {
            int p_id = posAtt->mapped_index(face[j]).value();
            int t_id = texAtt->mapped_index(face[j]).value();

            std::array<float, 3> value;
            posAtt->ConvertValue<float, 3>(draco::AttributeValueIndex(p_id),
                                           &value[0]);
            bool is_new_tid = true;
            if (!pos_tex_maps[value].empty()) {
              if (pos_tex_maps[value].find(t_id)
                  != pos_tex_maps[value].end()) {
                is_new_tid = false;
              }
            }
            if (is_new_tid) {
              if (!pos_tex_maps[value].empty()
                  && pos_value_ids_maps[value].size()
                       <= pos_tex_maps[value].size()) {
                dst.addPoint(value[0], value[1], value[2]);
                //dst.addPoint(p_id);
              }
              pos_tex_maps[value].insert(t_id);
            }
          }
        }
      }
    }
  }
  // Normal
  if (nrmAtt) {
    for (draco::AttributeValueIndex i(0);
         i < static_cast<uint32_t>(nrmAtt->size());
         ++i) {
      std::array<int32_t, 3> value{};
      if (!nrmAtt->ConvertValue<int32_t, 3>(i, value.data())) { return; }
      dst.addNormal(value[0], value[1], value[2]);
    }
  }
  // Color
  if (colAtt) {
    for (draco::AttributeValueIndex i(0);
         i < static_cast<uint32_t>(colAtt->size());
         ++i) {
      std::array<int32_t, 3> value{};
      if (!colAtt->ConvertValue<int32_t, 3>(i, value.data())) { return; }
      dst.addColour(value[0], value[1], value[2]);
    }
  }
  // Texture coordinate
  if (texAtt) {
    for (draco::AttributeValueIndex i(0);
         i < static_cast<uint32_t>(texAtt->size());
         ++i) {
      std::array<int32_t, 2> value{};
      if (!texAtt->ConvertValue<int32_t, 2>(i, value.data())) { return; }
      dst.addTexCoord(value[0], value[1]);
    }
  }
  for (draco::FaceIndex i(0); i < mesh.num_faces(); ++i) {
    const auto&   face = mesh.face(i);
    const int32_t idx0 = posAtt->mapped_index(face[0]).value();
    const int32_t idx1 = posAtt->mapped_index(face[1]).value();
    const int32_t idx2 = posAtt->mapped_index(face[2]).value();
    dst.addTriangle(idx0, idx1, idx2);
    if (texAtt && texAtt->size() > 0) {
      const int32_t tex0 = texAtt->mapped_index(face[0]).value();
      const int32_t tex1 = texAtt->mapped_index(face[1]).value();
      const int32_t tex2 = texAtt->mapped_index(face[2]).value();
      dst.addTexCoordTriangle(tex0, tex1, tex2);
    }
    if (nrmAtt && nrmAtt->size() > 0) {
      const int32_t nrm0 = nrmAtt->mapped_index(face[0]).value();
      const int32_t nrm1 = nrmAtt->mapped_index(face[1]).value();
      const int32_t nrm2 = nrmAtt->mapped_index(face[2]).value();
      dst.addNormalTriangle(nrm0, nrm1, nrm2);
    }
    if (fidAtt) {
      const uint32_t         fIdx = fidAtt->mapped_index(face[0]).value();
      std::array<int32_t, 1> value{};
      if (!fidAtt->ConvertValue<int32_t, 1>(draco::AttributeValueIndex(fIdx),
                                            value.data())) {
        return;
      }
      dst.addFaceId(value[0]);
    }
  }
}

template<typename T>
void
DracoGeometryEncoder<T>::encode(TriangleMesh<T>&           src,
                                GeometryEncoderParameters& params,
                                std::vector<uint8_t>&      bitstream,
                                TriangleMesh<T>&           rec)

{
  // Load draco mesh
  auto mesh = convert(src, params.dracoMeshLossless_);

  // Encode
  draco::Encoder encoder;
  encoder.SetSpeedOptions(10 - params.cl_, 10 - params.cl_);
  encoder.SetEncodingMethod(
    draco::MeshEncoderMethod::MESH_EDGEBREAKER_ENCODING);
  encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION,
                                   params.qp_);
  encoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD,
                                   params.qt_);
  if (params.qn_ > 0) {
    encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL,
                                     params.qn_);
  }
  if (params.qg_ > 0) {
    encoder.SetAttributeQuantization(draco::GeometryAttribute::COLOR,
                                     params.qg_);
  }
  encoder.options().SetGlobalBool("use_position", params.dracoUsePosition_);
  encoder.options().SetGlobalBool("use_uv", params.dracoUseUV_);
  encoder.options().SetGlobalBool("mesh_lossless", params.dracoMeshLossless_);
  draco::EncoderBuffer buffer;
  const draco::Status  status =
    encoder.EncodeMeshToBuffer(*(mesh.get()), &buffer);
  if (!status.ok()) {
    printf("Failed to encode the mesh: %s\n", status.error_msg());
    exit(-1);
  }
  printf("EncodeMeshToBuffer => buffer size = %zu \n", buffer.size());
  fflush(stdout);

  // Copy bitstream
  bitstream.resize(buffer.size() + 1, 0);
  bitstream[0] = static_cast<int>(params.dracoUsePosition_)
                 | (static_cast<int>(params.dracoUseUV_) << 1)
                 | (static_cast<int>(params.dracoMeshLossless_) << 2);
  std::copy(
    buffer.data(), buffer.data() + buffer.size(), bitstream.data() + 1);

  // Decode
  draco::DecoderBuffer decBuffer;
  decBuffer.Init((const char*)bitstream.data() + 1, bitstream.size() - 1);
  auto type = draco::Decoder::GetEncodedGeometryType(&decBuffer);
  if (!type.ok()) {
    printf("Failed GetEncodedGeometryType: %s.\n", type.status().error_msg());
    exit(-1);
  }
  if (type.value() == draco::TRIANGULAR_MESH) {
    draco::Decoder decoder;
    // Draco addition parameters:
    //  - draco use position mode (m60340)
    //  - draco use uv mode (m60293)
    //  - whether mesh is lossless (m60289)
    auto*      options      = decoder.options();
    const bool usePosition  = ((bitstream[0] >> 0) & 1) != 0;
    const bool useUv        = ((bitstream[0] >> 1) & 1) != 0;
    const bool meshLossless = ((bitstream[0] >> 2) & 1) != 0;
    decoder.options()->SetGlobalBool("use_position", usePosition);
    decoder.options()->SetGlobalBool("use_uv", useUv);
    decoder.options()->SetGlobalBool("mesh_lossless", meshLossless);
    auto status = decoder.DecodeMeshFromBuffer(&decBuffer);
    if (!status.ok()) {
      printf("Failed DecodeMeshFromBuffer: %s.\n",
             status.status().error_msg());
      exit(-1);
    }
    std::unique_ptr<draco::Mesh> decMesh = std::move(status).value();
    if (decMesh) {
      convert(decMesh, rec, meshLossless);
    } else {
      printf("Failed no in mesh  \n");
      exit(-1);
    }
  } else {
    printf("Failed no mesh type not supported.\n");
    exit(-1);
  }
}

template class DracoGeometryEncoder<float>;
template class DracoGeometryEncoder<double>;

}  // namespace vmesh

#endif
