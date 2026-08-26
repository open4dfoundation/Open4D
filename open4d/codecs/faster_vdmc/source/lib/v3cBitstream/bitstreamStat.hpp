/* The copyright in this software is being made available under the BSD
* License, included below. This software may be subject to other third party
* and contributor rights, including patent rights, and no such rights are
* granted under this license.
*
* Copyright (c) 2010-2017, ISO/IEC
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
*  * Neither the name of the ISO/IEC nor the names of its contributors may
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
#pragma once

#include "videoBitstream.hpp"
#include "util/memory.hpp"
#include "util/logger.hpp"
#include "vmc.hpp"
#include "acDisplacementCommon.hpp"
#include "acDisplacementStat.hpp"
#include "baseMeshStat.hpp"

namespace vmesh {

class BitstreamGofStat {
public:
  BitstreamGofStat() {
    v3cUnitSize_.resize(NUM_V3C_UNIT_TYPE, 0);
    videoBinSize_.resize(NUM_VIDEO_TYPE, 0);
    //baseMeshBinSize_.resize(basemesh::RESERVED_BASEMESH + 1, 0);
    //displacementBinSize_.resize(acdisplacement::RESERVED_DISPL + 1, 0);
  }
  ~BitstreamGofStat() {}
  void overwriteV3CUnitSize(V3CUnitType type, size_t size) {
    v3cUnitSize_[type] = size;
  }
  void resetBaseMeshBinSize() {
    //for (auto& el : baseMeshBinSize_) el = 0;
    basemeshBitstreamGofStat_.resetBaseMeshBinSize();
  }
  void resetDisplacementBinSize() {
    //for (auto& el : displacementBinSize_) el = 0;
    acDisplacementBitstreamGofStat_.resetDisplacementBinSize();
  }
  auto& getFaceCount() { return basemeshBitstreamGofStat_.getFaceCount(); }
  void  setV3CUnitSize(V3CUnitType type, size_t size) {
    v3cUnitSize_[type] += size;
  }
  void setVideo(const VideoBitstream& video) {
    videoBinSize_[video.type()] += video.size();
  }
  void setBaseMesh(basemesh::BaseMeshType type, size_t size) {
    //baseMeshBinSize_[type] += size;
    basemeshBitstreamGofStat_.setBaseMesh(type, size);
  }
  void setDisplacement(acdisplacement::DisplacementType type, size_t size) {
    //displacementBinSize_ [type] += size;
    acDisplacementBitstreamGofStat_.setDisplacement(type, size);
  }
  size_t getV3CUnitSize(V3CUnitType type) { return v3cUnitSize_[type]; }
  size_t getVideoBinSize(VideoType type) { return videoBinSize_[type]; }
  BitstreamGofStat& operator+=(const BitstreamGofStat& other) {
    for (int i = 0; i < NUM_V3C_UNIT_TYPE; i++) {
      v3cUnitSize_[i] += other.v3cUnitSize_[i];
    }
    for (int i = 0; i < NUM_VIDEO_TYPE; i++) {
      videoBinSize_[i] += other.videoBinSize_[i];
    }
    //for (int i = 0; i < basemesh::RESERVED_BASEMESH + 1; i++) {
    //  baseMeshBinSize_[i] += other.baseMeshBinSize_[i];
    //}
    basemeshBitstreamGofStat_ += other.basemeshBitstreamGofStat_;
    //   for (int i = 0; i < acdisplacement::RESERVED_DISPL + 1; i++) {
    //     displacementBinSize_[i] += other.displacementBinSize_[i];
    //   }
    //faceCount        += other.faceCount;
    acDisplacementBitstreamGofStat_ += other.acDisplacementBitstreamGofStat_;
    return *this;
  }
  size_t getTotal() {
    return v3cUnitSize_[V3C_VPS] + v3cUnitSize_[V3C_AD] + v3cUnitSize_[V3C_BMD]
           + v3cUnitSize_[V3C_OVD] + v3cUnitSize_[V3C_GVD]
           + v3cUnitSize_[V3C_PVD] + v3cUnitSize_[V3C_AVD]
           + v3cUnitSize_[V3C_ADD];
  }
  size_t getTotalGeometry() {
    size_t retVal = 0;
    for (int i = VIDEO_GEOMETRY; i <= VIDEO_GEOMETRY_RAW; i++)
      retVal += videoBinSize_[i];
    return retVal;
  }
  size_t getTotalAttribute() {
    size_t retVal = 0;
    for (int i = VIDEO_ATTRIBUTE;
         i < VIDEO_ATTRIBUTE_RAW + MAX_NUM_ATTR_PARTITIONS;
         i++)
      retVal += videoBinSize_[i];
    return retVal;
  }
  size_t getTotalPacked() {
    size_t retVal = 0;
    for (int i = VIDEO_PACKED; i <= VIDEO_PACKED; i++)
      retVal += videoBinSize_[i];
    return retVal;
  }
  size_t getTotalBaseMesh() {
    //return baseMeshBinSize_[basemesh::I_BASEMESH] + baseMeshBinSize_[basemesh::P_BASEMESH];
    return basemeshBitstreamGofStat_.getTotalBaseMesh();
  }
  size_t getTotalBaseMeshIntra() {
    //return baseMeshBinSize_[basemesh::I_BASEMESH];
    return basemeshBitstreamGofStat_.getTotalBaseMeshIntra();
  }
  size_t getTotalBaseMeshInter() {
    //return baseMeshBinSize_[basemesh::P_BASEMESH];
    return basemeshBitstreamGofStat_.getTotalBaseMeshInter();
  }
  size_t getTotalDisplacement() {
//    return displacementBinSize_[acdisplacement::I_DISPL]
//           + displacementBinSize_[acdisplacement::P_DISPL];
    return acDisplacementBitstreamGofStat_.getTotalDisplacement();
  }
  size_t getDisplacementIntra() {
//    return displacementBinSize_[acdisplacement::I_DISPL];
    return acDisplacementBitstreamGofStat_.getDisplacementIntra();
  }
  size_t getDisplacementInter() {
//    return displacementBinSize_[acdisplacement::P_DISPL];
    return acDisplacementBitstreamGofStat_.getDisplacementInter();
  }
  auto& getBaseMeshStat() const { return basemeshBitstreamGofStat_; }
  auto& getBaseMeshStat() { return basemeshBitstreamGofStat_; }

  auto& getAcDisplacementStat() const { return acDisplacementBitstreamGofStat_; }
  auto& getAcDisplacementStat() { return acDisplacementBitstreamGofStat_; }

  size_t getTotalMetadata() {
    return v3cUnitSize_[V3C_VPS] + v3cUnitSize_[V3C_AD]
           + (v3cUnitSize_[V3C_BMD] - getTotalBaseMesh())
           + (v3cUnitSize_[V3C_ADD] - getTotalDisplacement())
           + (v3cUnitSize_[V3C_GVD] - getTotalGeometry())
           + (v3cUnitSize_[V3C_AVD] - getTotalAttribute())
           + (v3cUnitSize_[V3C_PVD] - getTotalPacked());
  }

  void trace() {
    printf("    V3CUnitSize[ V3C_VPS ]: %9zu B %9zu b\n",
           v3cUnitSize_[V3C_VPS],
           v3cUnitSize_[V3C_VPS] * 8);
    printf("    V3CUnitSize[ V3C_AD  ]: %9zu B %9zu b\n",
           v3cUnitSize_[V3C_AD],
           v3cUnitSize_[V3C_AD] * 8);
    printf("    V3CUnitSize[ V3C_BMD ]: %9zu B %9zu b ( Header = %9zu B +"
           " Intra = %9zu B + Inter = %9zu B )\n",
           v3cUnitSize_[V3C_BMD],
           v3cUnitSize_[V3C_BMD] * 8,
           v3cUnitSize_[V3C_BMD] - getTotalBaseMesh(),
           getTotalBaseMeshIntra(),
           getTotalBaseMeshInter());
    printf("    V3CUnitSize[ V3C_GVD ]: %9zu B %9zu b ( Header = %9zu B + "
           "Video = %9zu B)\n",
           v3cUnitSize_[V3C_GVD],
           v3cUnitSize_[V3C_GVD] * 8,
           v3cUnitSize_[V3C_GVD] - getTotalGeometry(),
           getTotalGeometry());
    printf("    V3CUnitSize[ V3C_AVD ]: %9zu B %9zu b ( Header = %9zu B + "
           "Video = %9zu B)\n",
           v3cUnitSize_[V3C_AVD],
           v3cUnitSize_[V3C_AVD] * 8,
           v3cUnitSize_[V3C_AVD] - getTotalAttribute(),
           getTotalAttribute());
    printf("    V3CUnitSize[ V3C_PVD ]: %9zu B %9zu b ( Header = %9zu B + "
           "Video = %9zu B)\n",
           v3cUnitSize_[V3C_PVD],
           v3cUnitSize_[V3C_PVD] * 8,
           v3cUnitSize_[V3C_PVD] - getTotalPacked(),
           getTotalPacked());
    printf("    V3CUnitSize[ V3C_ADD ]: %9zu B %9zu b ( Header = %9zu B + "
           "AC-based displacement = %9zu B)\n",
           v3cUnitSize_[V3C_ADD],
           v3cUnitSize_[V3C_ADD] * 8,
           v3cUnitSize_[V3C_ADD] - getTotalDisplacement(),
           getTotalDisplacement());
  }

private:
  std::vector<size_t> v3cUnitSize_;
  std::vector<size_t> videoBinSize_;
  acdisplacement::AcDisplacementBitstreamGofStat
                                     acDisplacementBitstreamGofStat_;
  basemesh::BaseMeshBitstreamGofStat basemeshBitstreamGofStat_;
  //std::vector<size_t> baseMeshBinSize_;
  //std::vector<size_t> displacementBinSize_;
  //size_t faceCount = 0;
};

class BitstreamStat {
public:
  BitstreamStat() { header_ = 0; }
  ~BitstreamStat() { bitstreamGofStat_.clear(); }
  void newGOF() {
    BitstreamGofStat element;
    bitstreamGofStat_.push_back(element);
  }
  void setHeader(size_t size) { header_ = size; }
  void incrHeader(size_t size) { header_ += size; }
  void overwriteV3CUnitSize(V3CUnitType type, size_t size) {
    bitstreamGofStat_.back().overwriteV3CUnitSize(type, size);
  }
  void resetBaseMeshBinSize() {
    bitstreamGofStat_.back().resetBaseMeshBinSize();
  }
  void setV3CUnitSize(V3CUnitType type, size_t size) {
    bitstreamGofStat_.back().setV3CUnitSize(type, size);
  }
  void setVideo(const VideoBitstream& video) {
    bitstreamGofStat_.back().setVideo(video);
  }
//  void setBaseMesh(basemesh::BaseMeshType type, size_t size) {
//    bitstreamGofStat_.back().setBaseMesh(type, size);
//  }
//  void setDisplacement(acdisplacement::DisplacementType type, size_t size) {
//    bitstreamGofStat_.back().setDisplacement(type, size);
//  }

  auto& getBaseMeshStat() const { return bitstreamGofStat_.back().getBaseMeshStat(); }
  auto& getBaseMeshStat() { return bitstreamGofStat_.back().getBaseMeshStat(); }

  auto& getAcDisplacementStat() const { return bitstreamGofStat_.back().getAcDisplacementStat(); }
  auto& getAcDisplacementStat() { return bitstreamGofStat_.back().getAcDisplacementStat(); }

  auto&  getFaceCount() { return bitstreamGofStat_.back().getFaceCount(); }
  size_t getV3CUnitSize(V3CUnitType type) {
    return bitstreamGofStat_.back().getV3CUnitSize(type);
  }
  size_t getVideoBinSize(VideoType type) {
    return bitstreamGofStat_.back().getVideoBinSize(type);
  }
  size_t getTotalGeometry() {
    return bitstreamGofStat_.back().getTotalGeometry();
  }
  size_t getTotalAttribute() {
    return bitstreamGofStat_.back().getTotalAttribute();
  }
  size_t getTotalPacked() { return bitstreamGofStat_.back().getTotalPacked(); }
  size_t getTotalMetadata() {
    return bitstreamGofStat_.back().getTotalMetadata();
  }

  void trace(bool byGOF = false) {
    if (!byGOF) printf("\n------- All frames -----------\n");
    printf("Bitstream stat: \n");
    printf(
      "  V3CHeader:                %9zu B %9zu b\n", header_, header_ * 8);
    if (byGOF) {
      for (size_t i = 0; i < bitstreamGofStat_.size(); i++) {
        printf("  Gof %2zu: \n", i);
        bitstreamGofStat_[i].trace();
      }
      printf("  All gofs: \n");
    }
    BitstreamGofStat allGof;
    for (auto& element : bitstreamGofStat_) { allGof += element; }
    allGof.trace();
    auto totalMetadata = allGof.getTotalMetadata() + header_;
    auto totalBaseMesh = allGof.getTotalBaseMesh();
    auto totalGeometry =
      allGof.getTotalGeometry() + allGof.getTotalDisplacement();
    auto totalAttribute = allGof.getTotalAttribute();
    auto totalPacked    = allGof.getTotalPacked();
    auto total          = allGof.getTotal() + header_;
    printf("  By types \n");
    printf("    Metadata:               %9zu B %9zu b \n",
           totalMetadata,
           totalMetadata * 8);
    printf("    BaseMesh:               %9zu B %9zu b \n",
           totalBaseMesh,
           totalBaseMesh * 8);
    printf("    Displacement:           %9zu B %9zu b \n",
           totalGeometry,
           totalGeometry * 8);
    printf("    Attribute:              %9zu B %9zu b \n",
           totalAttribute,
           totalAttribute * 8);
    printf("    Packed:                 %9zu B %9zu b \n",
           totalPacked,
           totalPacked * 8);
    printf("    Total:                  %9zu B %9zu b \n", total, total * 8);
    if (!byGOF) {
      traceTime();
      auto duration_1 = std::max(0., getTime("preprocess"))
                        + std::max(0., getTime("compress"))
                        + std::max(0., getTime("compressGeometry"))
                        + std::max(0., getTime("compressAttribute"))
                        + std::max(0., getTime("compressPacked"))
                        + std::max(0., getTime("writer"));
      auto duration_2 =
        std::max(0., getTime("decompress")) + std::max(0., getTime("reader"));
      auto duration = std::max(duration_1, duration_2);
      printf("Sequence processing time   %9f s \n", duration);
      printf("Sequence peak memory       %9d KB\n", getPeakMemory());
      printf("Sequence face count        %9zu\n", allGof.getFaceCount());
      printf("---------------------------------------\n");
    }
  }

private:
  size_t                        header_;
  std::vector<BitstreamGofStat> bitstreamGofStat_;
};

}  // namespace vmesh
