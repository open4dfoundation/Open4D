/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2025, ISO/IEC
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

#include "v3cCommon.hpp"
#include "videoBitstream.hpp"
#include "bitstream.hpp"
#include "v3cUnit.hpp"
#include "sampleStreamNalUnit.hpp"
#include "sampleStreamV3CUnit.hpp"
#include "v3cParameterSet.hpp"
#include "v3cUnitHeader.hpp"
#include "atlasBitstream.hpp"
#include "baseMeshBitstream.hpp"
#include "acDisplacementBitstream.hpp"

namespace vmesh {

class BitstreamStat;

class V3cBitstream {
public:
  V3cBitstream() {}
  ~V3cBitstream() {}

  // bitstream statistic related functions
  void setBitstreamStat(BitstreamStat& bitstreamStat) {
    bitstreamStat_ = &bitstreamStat;
  }
  BitstreamStat& getBitstreamStat() { return *bitstreamStat_; }

  auto& getActiveVpsId() { return activeVPS_; }
  auto& getAtlasId() { return atlasId_; }
  auto& getAtlasId() const { return atlasId_; }

  auto& getAtlasDataStream() const { return atlasDataBareStream_; }
  auto& getAtlasDataStream() { return atlasDataBareStream_; }

  auto& getBaseMeshDataStream() const { return baseMeshBareStream_; }
  auto& getBaseMeshDataStream() { return baseMeshBareStream_; }

  auto& getDisplacementStream() const { return acDisplacementStream_; }
  auto& getDisplacementStream() { return acDisplacementStream_; }

  //video
  std::vector<VideoBitstream>& getV3CUnitAVD() {
    return videoAttributeBitstream_;
  }
  std::vector<VideoBitstream>& getV3CUnitGVD() {
    return videoGeometryBitstream_;
  }
  std::vector<VideoBitstream>& getV3CUnitOVD() {
    return videoOccupancyBitstream_;
  }
  std::vector<VideoBitstream>& getV3CUnitPVD() {
    return videoPackedBitstream_;
  }

  VideoBitstream& getV3CUnitAVD(size_t index) {
    return videoAttributeBitstream_[index];
  }
  VideoBitstream& getV3CUnitGVD(size_t index) {
    return videoGeometryBitstream_[index];
  }
  VideoBitstream& getV3CUnitOVD(size_t index) {
    return videoOccupancyBitstream_[index];
  }
  VideoBitstream& getV3CUnitPVD(size_t index) {
    return videoPackedBitstream_[index];
  }

  const VideoBitstream& getV3CUnitAVD(size_t index) const {
    return videoAttributeBitstream_[index];
  }
  const VideoBitstream& getV3CUnitGVD(size_t index) const {
    return videoGeometryBitstream_[index];
  }
  const VideoBitstream& getV3CUnitOVD(size_t index) const {
    return videoOccupancyBitstream_[index];
  }
  const VideoBitstream& getV3CUnitPVD(size_t index) const {
    return videoPackedBitstream_[index];
  }

  VideoBitstream& addVideoSubstream(V3CUnitType substreamType) {
    if (substreamType == V3C_GVD) {
      VideoBitstream videoStream(VIDEO_GEOMETRY_D0);
      videoStream.setVuhMapIndex(0);
      videoStream.setVuhAuxiliaryVideoFlag(0);
      videoStream.setVuhAttributeIndex(0);
      videoStream.setVuhAttributePartitionIndex(0);
      videoGeometryBitstream_.push_back(videoStream);
      return videoGeometryBitstream_.back();
    } else if (substreamType == V3C_AVD) {
      VideoBitstream videoStream(VIDEO_ATTRIBUTE_T0);
      videoStream.setVuhMapIndex(0);
      videoStream.setVuhAuxiliaryVideoFlag(0);
      videoStream.setVuhAttributeIndex(0);
      videoStream.setVuhAttributePartitionIndex(0);
      videoAttributeBitstream_.push_back(videoStream);
      return videoAttributeBitstream_.back();
    } else if (substreamType == V3C_OVD) {
      VideoBitstream videoStream(VIDEO_OCCUPANCY);
      videoStream.setVuhMapIndex(0);
      videoStream.setVuhAuxiliaryVideoFlag(0);
      videoStream.setVuhAttributeIndex(0);
      videoStream.setVuhAttributePartitionIndex(0);
      videoOccupancyBitstream_.push_back(videoStream);
      return videoOccupancyBitstream_.back();
    } else if (substreamType == V3C_PVD) {
      VideoBitstream videoStream(VIDEO_PACKED);
      videoStream.setVuhMapIndex(0);
      videoStream.setVuhAuxiliaryVideoFlag(0);
      videoStream.setVuhAttributeIndex(0);
      videoStream.setVuhAttributePartitionIndex(0);
      videoPackedBitstream_.push_back(videoStream);
      return videoPackedBitstream_.back();
    } else {
      std::cout << "unknow substream type : " << substreamType << "\n";
      return videoGeometryBitstream_.back();
    }
  }

private:
  uint8_t activeVPS_ = 0;
  uint8_t atlasId_   = 0;

  atlas::AtlasBitstream          atlasDataBareStream_;
  basemesh::BaseMeshBitstream       baseMeshBareStream_;
  acdisplacement::AcDisplacementBitstream acDisplacementStream_;
  uint8_t                     geometryVideoEncoderId_ = 0;
  uint8_t                     textureVideoEncoderId_  = 0;
  std::vector<VideoBitstream> videoGeometryBitstream_;
  std::vector<VideoBitstream> videoAttributeBitstream_;
  std::vector<VideoBitstream> videoOccupancyBitstream_;
  std::vector<VideoBitstream> videoPackedBitstream_;

  BitstreamStat* bitstreamStat_;
};

};  // namespace vmesh
