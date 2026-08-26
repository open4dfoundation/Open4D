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
#  include "dracoGeometryDecoder.hpp"
#  include <unordered_map>
#  include <set>
#  include "draco/compression/decode.h"
#  include "draco/core/data_buffer.h"

namespace vmesh {

template<typename T>
DracoGeometryDecoder<T>::DracoGeometryDecoder() = default;
template<typename T>
DracoGeometryDecoder<T>::~DracoGeometryDecoder() = default;

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
DracoGeometryDecoder<T>::decode(const std::vector<uint8_t>& bitstream,
                                GeometryDecoderParameters&  params,
                                TriangleMesh<T>&            dec,
                                uint8_t&                    qpPosition,
                                uint8_t&                    qpNormal) {
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
      convert(decMesh, dec, meshLossless);
    } else {
      printf("Failed no in mesh  \n");
      exit(-1);
    }
  } else {
    printf("Failed no mesh type not supported.\n");
    exit(-1);
  }
}

template class DracoGeometryDecoder<float>;
template class DracoGeometryDecoder<double>;

}  // namespace vmesh

#endif
