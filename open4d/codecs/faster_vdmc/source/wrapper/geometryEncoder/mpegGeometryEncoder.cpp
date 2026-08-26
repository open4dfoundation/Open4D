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
#include "mpegGeometryEncoder.hpp"

#include "ebModel.h"
#include "ebChrono.h"
#include "ebReversiEncoder.h"
#include "ebDecoder.h"

namespace vmesh {

template<typename T>
MpegGeometryEncoder<T>::MpegGeometryEncoder() = default;
template<typename T>
MpegGeometryEncoder<T>::~MpegGeometryEncoder() = default;

//============================================================================

template<typename T>
void
convert(const TriangleMesh<T>& src, eb::Model& dst) {
  dst.reset();

  const auto&         points  = src.points();
  std::vector<float>* dpoints = new std::vector<float>(points.size() * 3);
  for (size_t i = 0; i < points.size(); i++) {
    (*dpoints)[3 * i + 0] = points[i][0];
    (*dpoints)[3 * i + 1] = points[i][1];
    (*dpoints)[3 * i + 2] = points[i][2];
  }

  const auto&           triangles = src.triangles();
  std::vector<int32_t>* dtriangles =
    new std::vector<int32_t>(triangles.size() * 3);
  for (size_t i = 0; i < triangles.size(); i++) {
    (*dtriangles)[3 * i + 0] = triangles[i][0];
    (*dtriangles)[3 * i + 1] = triangles[i][1];
    (*dtriangles)[3 * i + 2] = triangles[i][2];
  }
  std::shared_ptr<eb::MeshAttribute> attrPosPtr =
    std::make_shared<eb::MeshAttribute>(eb::AttrType::POSITION,
                                        eb::DataType::FLOAT32,
                                        3,
                                        eb::AttrDomain::PER_VERTEX,
                                        dpoints,
                                        dtriangles);

  dst.attributes.emplace_back(attrPosPtr);

  if (!src.texCoords().empty()) {
    const auto&         texCoords = src.texCoords();
    std::vector<float>* dtexCoords =
      new std::vector<float>(texCoords.size() * 2);
    for (size_t i = 0; i < texCoords.size(); i++) {
      (*dtexCoords)[2 * i + 0] = texCoords[i][0];
      (*dtexCoords)[2 * i + 1] = texCoords[i][1];
    }
    std::vector<int32_t>* dtexCoordsTri = new std::vector<int32_t>();
    if (!src.texCoordTriangles().empty()) {
      const auto& texCoordsTri = src.texCoordTriangles();
      dtexCoordsTri->resize(texCoordsTri.size() * 3);
      for (size_t i = 0; i < texCoordsTri.size(); i++) {
        (*dtexCoordsTri)[3 * i + 0] = texCoordsTri[i][0];
        (*dtexCoordsTri)[3 * i + 1] = texCoordsTri[i][1];
        (*dtexCoordsTri)[3 * i + 2] = texCoordsTri[i][2];
      }
    }

    std::shared_ptr<eb::MeshAttribute> attrTexCoordPtr =
      std::make_shared<eb::MeshAttribute>(
        eb::AttrType::TEXCOORD,
        eb::DataType::FLOAT32,
        2,
        eb::AttrDomain::PER_VERTEX,
        dtexCoords,
        dtexCoordsTri,
        src.texCoordTriangles().empty() ? 0 : -1);

    dst.attributes.emplace_back(attrTexCoordPtr);
  }

  if (!src.normals().empty()) {
    const auto&         normals  = src.normals();
    std::vector<float>* dnormals = new std::vector<float>(normals.size() * 3);
    for (size_t i = 0; i < normals.size(); i++) {
      (*dnormals)[3 * i + 0] = normals[i][0];
      (*dnormals)[3 * i + 1] = normals[i][1];
      (*dnormals)[3 * i + 2] = normals[i][2];
    }
    std::vector<int32_t>* dnormalsTri = new std::vector<int32_t>();
    if (!src.normalTriangles().empty()) {
      const auto& normalsTri = src.normalTriangles();
      dnormalsTri->resize(normalsTri.size() * 3);
      for (size_t i = 0; i < normalsTri.size(); i++) {
        (*dnormalsTri)[3 * i + 0] = normalsTri[i][0];
        (*dnormalsTri)[3 * i + 1] = normalsTri[i][1];
        (*dnormalsTri)[3 * i + 2] = normalsTri[i][2];
      }
    }

    std::shared_ptr<eb::MeshAttribute> attrNormalsPtr =
      std::make_shared<eb::MeshAttribute>(eb::AttrType::NORMAL,
                                          eb::DataType::FLOAT32,
                                          3,
                                          eb::AttrDomain::PER_VERTEX,
                                          dnormals,
                                          dnormalsTri,
                                          src.normalTriangles().empty() ? 0
                                                                        : -1);

    dst.attributes.emplace_back(attrNormalsPtr);
  }
  if (!src.faceIds().empty()) {
    const auto&           faceIds  = src.faceIds();
    std::vector<int32_t>* dfaceIds = new std::vector<int32_t>();
    dfaceIds->resize(faceIds.size());
    for (size_t i = 0; i < faceIds.size(); i++) {
      (*dfaceIds)[i] = faceIds[i];
    }

    std::shared_ptr<eb::MeshAttribute> attrNormalsPtr =
      std::make_shared<eb::MeshAttribute>(eb::AttrType::MATERIAL_ID,
                                          eb::DataType::INT32,
                                          1,
                                          eb::AttrDomain::PER_FACE,
                                          dfaceIds);

    dst.attributes.emplace_back(attrNormalsPtr);
  }
}

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

#define COUT \
  if (params.verbose) std::cout << "EB"

template<typename T>
void
MpegGeometryEncoder<T>::encode(TriangleMesh<T>&           src,
                               GeometryEncoderParameters& params,
                               std::vector<uint8_t>&      bitstream,
                               TriangleMesh<T>&           rec)

{
  // convert source model
  eb::Model srcModel;
  convert(src, srcModel);

  // prepare encoder parameters
  eb::EBReversiEncoder encoder;

  // prepare encoder parameters
  encoder.cfg.intAttr = true;
  encoder.cfg.verbose = params.verbose;
  encoder.qp          = params.qp_;
  encoder.qt          = params.qt_;
  encoder.qm          = params.qg_;  // materialId used ustead of generic
  encoder.qn          = params.qn_;
  encoder.qg          = params.qg_;
  switch (params.predNormal) {
  case 1: encoder.cfg.normPred = eb::EBConfig::NormPred::DELTA; break;
  case 2: encoder.cfg.normPred = eb::EBConfig::NormPred::MPARA; break;
  case 3: encoder.cfg.normPred = eb::EBConfig::NormPred::CROSS; break;
  }
  switch (params.predGeneric) {
  case 0: encoder.cfg.genPred = eb::EBConfig::GenPred::DELTA; break;
  case 1: encoder.cfg.genPred = eb::EBConfig::GenPred::MPARA; break;
  }
  encoder.cfg.qpOcta           = params.qpOcta_;
  encoder.cfg.useOctahedral    = params.normalsOctahedral;
  encoder.cfg.useEntropyPacket = params.entropyPacket;
  encoder.cfg.traversal        = eb::EBConfig::Traversal::DEGREE;
  encoder.cfg.posPred          = eb::EBConfig::PosPred::MPARA;
  encoder.cfg.uvPred           = eb::EBConfig::UvPred::STRETCH;

  if (params.traversal_ == "eb")
    encoder.cfg.traversal = eb::EBConfig::Traversal::EB;
  else if (params.traversal_ == "degree")
    encoder.cfg.traversal = eb::EBConfig::Traversal::DEGREE;

  // the reindexing scheme is signalled in the bitstream
  if (params.reindexOutput_ == "default")
    encoder.cfg.reindexOutput = eb::EBConfig::Reindex::EB;
  else if (params.reindexOutput_ == "degree")
    encoder.cfg.reindexOutput = eb::EBConfig::Reindex::DEGREE;

  // deduplication is deactivated by default
  encoder.cfg.deduplicate        = params.baseMeshDeduplication_;
  encoder.cfg.reverseUnification = params.reverseUnification_;

  auto t = eb::now();
  // compress
  encoder.encode(srcModel);

  // serialize to bitstream
  eb::Bitstream bs;
#if defined(BITSTREAM_TRACE)
  eb::Logger loggerMeb;
  if (!params.logFileName_.empty()) {
    loggerMeb.initilalize(params.logFileName_ + "_meb", true);
    bs.setLogger(loggerMeb);
    bs.setTrace(true);
  }
#endif
  encoder.serialize(bs);

  int meshCodecHeaderLength = encoder.getMeshCodingHeaderLength();
  this->setMeshCodingHeaderLength(meshCodecHeaderLength);

  COUT << "  Total encoding time (ms) = " << eb::elapsed(t) << std::endl;

  // time profiling performs params.profile - 1 additional runs of encoding
  // skiped if params.profile <= 0 (default=0)
  for (auto nrun = 0; nrun < params.profile; ++nrun) {
    COUT << "  ** start MPEG EB encoding profiling, step= " << nrun
         << std::flush << std::endl;
    // prepare encoder
    eb::EBReversiEncoder pencoder;
    // prepare encoder parameters
    pencoder.cfg = encoder.cfg;
    pencoder.qt  = encoder.qt;
    pencoder.qp  = encoder.qp;
    pencoder.qg  = encoder.qg;
    pencoder.qm  = encoder.qm;
    // start additional run
    auto t = eb::now();
    pencoder.encode(srcModel);
    eb::Bitstream pbs;
    pencoder.serialize(pbs);
    COUT << "  Total encoding time (ms) = " << eb::elapsed(t) << std::endl;
  }

  // Decode for rec
  // parsing
  bs.beginning();
  eb::MeshCoding meshCoding;  // Top level syntax element
  eb::EbReader   ebReader;
#if defined(BITSTREAM_TRACE)
  eb::Logger loggerMebDec;
  if (!params.logFileName_.empty()) {
    loggerMebDec.initilalize(params.logFileName_ + "_meb");
    bs.setLogger(loggerMebDec);
  }
#endif
  ebReader.read(bs, meshCoding);

  const auto& mch = meshCoding.getMeshCodingHeader();
  // Codec Variant
  const auto& method = mch.getMeshCodecType();

  // instanciate decoder - single variant - will be moved inside the library
  eb::EBDecoder decoder;
  decoder.cfg.verbose = params.verbose;

  // provide parameters to the decoder
  decoder.cfg.reindexOutput = eb::EBConfig::Reindex::DEFAULT;

  t = eb::now();

  // deserialize the bitstream
  decoder.unserialize(meshCoding);

  // decode to model
  eb::Model recModel;
  decoder.decode(recModel);

  COUT << "  Total decoding time (ms) = " << eb::elapsed(t) << std::endl;

  // time profiling performs params.profile - 1 additional runs of decoding
  // skipped if params.profile <= 0 (default=0)
  for (auto nrun = 0; nrun < params.profile; ++nrun) {
    COUT << "  ** start MPEG EB decoding profiling, step= " << nrun
         << std::flush << std::endl;
    eb::EBDecoder pdecoder;
    pdecoder.cfg = decoder.cfg;
    t            = eb::now();
    pdecoder.unserialize(meshCoding);
    eb::Model precModel;
    pdecoder.decode(precModel);
    COUT << "  Total decoding time (ms) = " << eb::elapsed(t) << std::endl;
  }

  // convert and unify vertices
  convert(recModel, rec);

  // set bitstream
  std::swap(bitstream, bs.vector());
  bitstream.resize(bs.size());  // assumes the bitstream is byte aligned
}

template class MpegGeometryEncoder<float>;
template class MpegGeometryEncoder<double>;

}  // namespace vmesh
