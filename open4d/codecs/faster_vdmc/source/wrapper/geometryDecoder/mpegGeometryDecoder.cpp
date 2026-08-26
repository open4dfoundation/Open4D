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

#include "util/mesh.hpp"
#include "mpegGeometryDecoder.hpp"

#include "ebModel.h"
#include "ebDecoder.h"

#include "ebReader.hpp"

namespace vmesh {

template<typename T>
MpegGeometryDecoder<T>::MpegGeometryDecoder() = default;
template<typename T>
MpegGeometryDecoder<T>::~MpegGeometryDecoder() = default;

// NOTE : duplicated in  mpegLibGeometryEncoder for reconstruction
template<typename T>
void
convert(const eb::Model& src, TriangleMesh<T>& dst) {
  std::vector<float>   emptyFloat;
  std::vector<int32_t> emptyInt;

  // assuming float type
  std::vector<float>*   vertices    = nullptr;  // vertex positions (x,y,z)
  std::vector<float>*   uvcoords    = nullptr;  // vertex uv coordinates (u,v)
  std::vector<float>*   normals     = nullptr;  // vertex normals (x,y,z)
  std::vector<float>*   colors      = nullptr;  // vertex colors (r,g,b)
  std::vector<int32_t>* triangles   = nullptr;  // triangle position indices
  std::vector<int32_t>* trianglesuv = nullptr;  // triangle uv indices
  std::vector<int32_t>* trianglesnormal = nullptr;  // triangle normal indices

  std::vector<int32_t>* faceIds = nullptr;  // material id values assuming ints

  // when normals use the same index as positions, then leave the triangle index empty
  // to keep md5 aligned and simplify potential comparisons for equality later.
  // this could be extended to all cases using a shared index and would require to update the code
  // for the cases where the reference index is not the primary attribute (not handled in the current specification)
  // note that this is not done for tex coords
  bool forceEmptyNormalTriangles = true;

  for (const auto& attrPtr : src.attributes) {
    // current implementation assumes MATERIAL_ID attributes to be per face and others to be per vertex
    // current implementation assumes MATERIAL_ID attributes values to be integers and other attributes values to be float
    const eb::MeshAttribute* attr = attrPtr.get();
    if (!vertices && (attr->getType() == eb::AttrType::POSITION)
        && (attr->getDataType() == eb::DataType::FLOAT32)) {
      vertices = attr->getData<float>();
      triangles =
        attr->getIndices<int32_t>();
    }
    if (!uvcoords && (attr->getType() == eb::AttrType::TEXCOORD)
        && (attr->getDataType() == eb::DataType::FLOAT32)) {
      uvcoords = attr->getData<float>();
      if (attr->getIndexCnt()) trianglesuv = attr->getIndices<int32_t>();
    }
    // COLOR attributes are not implemented in current MEB version
    if (!colors && (attr->getType() == eb::AttrType::COLOR)
        && (attr->getDataType() == eb::DataType::FLOAT32)) {
      colors = attr->getData<float>();
    }
    if (!normals && (attr->getType() == eb::AttrType::NORMAL)
        && (attr->getDataType() == eb::DataType::FLOAT32)) {
      normals = attr->getData<float>();
      if (attr->getIndexCnt()) trianglesnormal = attr->getIndices<int32_t>();
      // CAUTION - md5 are altered when keeping the triangle indices even when there is a ref while meshes are identical
      forceEmptyNormalTriangles = (attr->getIndexRef() == 0);
    }
    if (!faceIds && (attr->getType() == eb::AttrType::MATERIAL_ID)
        && (attr->getDataType() == eb::DataType::INT32)) {
      faceIds = attr->getData<int32_t>();
    }
  }

  if (!vertices) vertices = &emptyFloat;      // vertex positions (x,y,z)
  if (!uvcoords) uvcoords = &emptyFloat;      // vertex uv coordinates (u,v)
  if (!normals) normals = &emptyFloat;        // vertex normals (x,y,z)
  if (!colors) colors = &emptyFloat;          // vertex colors (r,g,b)
  if (!triangles) triangles = &emptyInt;      // triangle position indices
  if (!trianglesuv) trianglesuv = &emptyInt;  // triangle uv indices
  if (!trianglesnormal) trianglesnormal = &emptyInt;  // normal indices
  if (!faceIds) faceIds = &emptyInt;                  // single mat id

  dst.resizePoints(vertices->size() / 3);
  auto& dpoints = dst.points();
  for (size_t i = 0; i < dpoints.size(); i++) {
    dpoints[i][0] = (*vertices)[3 * i + 0];
    dpoints[i][1] = (*vertices)[3 * i + 1];
    dpoints[i][2] = (*vertices)[3 * i + 2];
  }
  if (!uvcoords->empty()) {
    dst.resizeTexCoords(uvcoords->size() / 2);
    auto& dtexCoords = dst.texCoords();
    for (size_t i = 0; i < dtexCoords.size(); i++) {
      dtexCoords[i][0] = (*uvcoords)[2 * i + 0];
      dtexCoords[i][1] = (*uvcoords)[2 * i + 1];
    }
  }
  if (!normals->empty()) {
    dst.resizeNormals(normals->size() / 3);
    auto& dnormals = dst.normals();
    for (size_t i = 0; i < dnormals.size(); i++) {
      dnormals[i][0] = (*normals)[3 * i + 0];
      dnormals[i][1] = (*normals)[3 * i + 1];
      dnormals[i][2] = (*normals)[3 * i + 2];
    }
  }
  dst.resizeTriangles(triangles->size() / 3);
  auto& dtriangles = dst.triangles();
  for (size_t i = 0; i < dtriangles.size(); i++) {
    dtriangles[i][0] = (*triangles)[3 * i + 0];
    dtriangles[i][1] = (*triangles)[3 * i + 1];
    dtriangles[i][2] = (*triangles)[3 * i + 2];
  }
  if (!trianglesuv->empty()) {
    dst.resizeTexCoordTriangles(trianglesuv->size() / 3);
    auto& dtexCoordsTri = dst.texCoordTriangles();
    for (size_t i = 0; i < dtexCoordsTri.size(); i++) {
      dtexCoordsTri[i][0] = (*trianglesuv)[3 * i + 0];
      dtexCoordsTri[i][1] = (*trianglesuv)[3 * i + 1];
      dtexCoordsTri[i][2] = (*trianglesuv)[3 * i + 2];
    }
  } else if (!uvcoords->empty()) {
    dst.resizeTexCoordTriangles(dst.triangles().size());
    auto& dtexCoordsTri = dst.texCoordTriangles();
    auto& triangles     = dst.triangles();
    for (size_t i = 0; i < dtexCoordsTri.size(); i++) {
      dtexCoordsTri[i] = triangles[i];
    }
  }
  if (!forceEmptyNormalTriangles) {
    if (!trianglesnormal->empty()) {
      dst.resizeNormalTriangles(trianglesnormal->size() / 3);
      auto& dnormalTri = dst.normalTriangles();
      for (size_t i = 0; i < dnormalTri.size(); i++) {
        dnormalTri[i][0] = (*trianglesnormal)[3 * i + 0];
        dnormalTri[i][1] = (*trianglesnormal)[3 * i + 1];
        dnormalTri[i][2] = (*trianglesnormal)[3 * i + 2];
      }
    } else if (!normals->empty()) {
      dst.resizeNormalTriangles(dst.triangles().size());
      auto& dnormalTri = dst.normalTriangles();
      auto& triangles  = dst.triangles();
      for (size_t i = 0; i < dnormalTri.size(); i++) {
        dnormalTri[i] = triangles[i];
      }
    }
  }
  if (!faceIds->empty()) {
    dst.resizeFaceIds(faceIds->size());
    auto& dfaceIds = dst.faceIds();
    for (size_t i = 0; i < dfaceIds.size(); i++) {
      dfaceIds[i] = (*faceIds)[i];
    }
  }
}

template<typename T>
void
MpegGeometryDecoder<T>::decode(const std::vector<uint8_t>& bitstream,
                               GeometryDecoderParameters&  params,
                               TriangleMesh<T>&            dec,
                               uint8_t&                    qpPosition,
                               uint8_t&                    qpNormal) {
  // mm bitstream
  eb::Bitstream bs;
  // init buffer from input
  std::swap(const_cast<std::vector<uint8_t>&>(bitstream), bs.vector());
  //bs.initialize(bitstream); // init without copy to add

  // parsing
  eb::MeshCoding meshCoding;  // Top level syntax element
  eb::EbReader   ebReader;
  ebReader.read(bs, meshCoding);

  const auto& mch = meshCoding.getMeshCodingHeader();
  // Codec Variant
  const auto& method = mch.getMeshCodecType();

  // instanciate decoder
  eb::EBDecoder decoder;

  // deserialize the bitstream
  decoder.unserialize(meshCoding);

  // decode to model
  eb::Model decModel;
  decoder.decode(decModel);

  // convert back to triangle mesh
  convert(decModel, dec);

  // revert bitstream
  std::swap(const_cast<std::vector<uint8_t>&>(bitstream), bs.vector());
  //bitstream.resize(bs.size()); // assumes the bitstream is byte aligned

  qpPosition = 
    mch.getMeshPositionEncodingParameters().getMeshPositionBitDepthMinus1()
    + 1;

  for (auto i = 0; i < mch.getMeshAttributeCount(); i++) {
    if (mch.getMeshAttributeType()[i] == eb::MeshAttributeType::MESH_ATTR_NORMAL) { // MeshAttributeType::MESH_ATTR_NORMAL
        qpNormal = mch.getMeshAttributesEncodingParameters(i).getMeshAttributeBitDepthMinus1() + 1;
    }
  }
}

template class MpegGeometryDecoder<float>;
template class MpegGeometryDecoder<double>;

}  // namespace vmesh
