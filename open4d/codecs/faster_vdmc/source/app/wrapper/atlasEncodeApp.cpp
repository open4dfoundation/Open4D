/* The copyright in this software is being made available under the BSD
* Licence, included below.  This software may be subject to other third
* party and contributor rights, including patent rights, and no such
* rights are granted under this licence.
*
* Copyright (c) 2025, ISO/IEC
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.
*
* * Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
*
* * Neither the name of the ISO/IEC nor the names of its contributors
*   may be used to endorse or promote products derived from this
*   software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include "sequenceInfo.hpp"
#include "atlasBitstream.hpp"
#include "atlasWriter.hpp"
#include "atlasEncoder.hpp"
#include "bitstream.hpp"


const int frameCount                = 1;
const int subdivisionIterationCount = 1;

void
foo_tile_init(std::vector<std::vector<atlas::AtlasTile>>& tileAreasInVideo,
              std::pair<int32_t, int32_t>&                geometryVideoSize,
              std::vector<std::pair<uint32_t, uint32_t>>& attributeVideoSize) {
  // one for attribute tile, one for geometry tile
  int       patchCount          = 1;
  const int numTilesGeometry    = 1;
  const int numTilesAttribute   = 1;
  int       tileCount           = numTilesGeometry + numTilesAttribute;
  const int videoAttributeCount = 1;
  const int lodPatchesEnable    = 0;

  tileAreasInVideo.resize(numTilesGeometry + numTilesAttribute);
  for (uint32_t i = 0; i < tileCount; i++) {
    tileAreasInVideo[i].resize(frameCount);
    for (uint32_t f = 0; f < frameCount; f++) {
      if (i < numTilesGeometry) {
        tileAreasInVideo[i][f].tileType_ = atlas::I_TILE;
      } else {
        tileAreasInVideo[i][f].tileType_ = atlas::I_TILE_ATTR;
      }
      tileAreasInVideo[i][f].tileId_ = i;
      tileAreasInVideo[i][f].tileAttributeAreas_.resize(videoAttributeCount);
      tileAreasInVideo[i][f].tileGeometryArea_ =
        atlas::imageArea{(uint32_t)geometryVideoSize.first,
                         (uint32_t)geometryVideoSize.second,
                         0,
                         0};
      for (int attIdx = 0; attIdx < videoAttributeCount; attIdx++) {
        tileAreasInVideo[i][f].tileAttributeAreas_[attIdx] =
          atlas::imageArea{(uint32_t)attributeVideoSize[0].first,
                           (uint32_t)attributeVideoSize[0].second,
                           0,
                           0};
      }

      if (lodPatchesEnable == 1
          && tileAreasInVideo[i][f].tileType_ == atlas::I_TILE) {
        patchCount = patchCount * (subdivisionIterationCount + 1);
      }
      tileAreasInVideo[i][f].patches_.resize(patchCount);
      for (auto& patch : tileAreasInVideo[i][f].patches_) {
        patch.geometryPatchArea = atlas::imageArea{10, 23, 1, 3};
        patch.blockCount.resize(subdivisionIterationCount + 1);
        patch.lastPosInBlock.resize(subdivisionIterationCount + 1);
        for (int s = 0; s < subdivisionIterationCount + 1; s++) {
          patch.blockCount[s] = 0;
          patch.lastPosInBlock[s]   = 0;
        }
        patch.attributePatchArea.resize(videoAttributeCount);
        for (int attIdx = 0; attIdx < videoAttributeCount; attIdx++) {
          patch.attributePatchArea[attIdx] =
            atlas::imageArea{(uint32_t)attributeVideoSize[0].first,
                             (uint32_t)attributeVideoSize[0].second,
                             0,
                             0};
        }
      }
    }
  }
}

int
main(int argc, char* argv[]) {
  std::cout << "ISO/IEC 23090-29 atlas test encoder application" << '\n';

  auto compressedFilename = "/Users/kondrad/Code/data/vdmc/v12/dummy.atlas";
  auto logFilename = "/Users/kondrad/Code/data/vdmc/v12/dummy_hls_enc_atlas";

  std::vector<std::vector<vmesh::VMCSubmesh>> submeshes;
  submeshes.resize(1);
  submeshes[0].resize(frameCount);
  submeshes[0][0].submeshType = basemesh::I_BASEMESH;
  submeshes[0][0].disp.resize(100);
  vmesh::Vec3<double> point;
  submeshes[0][0].base.addPoint(point);
  // TODO this is used in atlasEncoder for allocation, but the allocation shoould be based on encoder parameters
  submeshes[0][0].subdivInfoLevelOfDetails.resize(subdivisionIterationCount + 1);

    vmesh::Logger loggerHls;

  vmesh::Bitstream              bitstream;
  atlas::AtlasWriter            atlasWriter;
  atlas::AtlasEncoder           atlasEncoder;
  atlas::AtlasBitstream         atlasStream;
  atlas::AtlasEncoderParameters atlasEncoderParams;

  loggerHls.initilalize(logFilename, true);
#if defined (BITSTREAM_TRACE)
  atlasWriter.setLogger(loggerHls);
  bitstream.setLogger(loggerHls);
  bitstream.setTrace(true);
#endif

  atlasStream.setAtlasId(0);
  atlasStream.setV3CParameterSetId(0);

  atlasEncoderParams.iDeriveTextCoordFromPos = 0;
  atlasEncoderParams.transformMethod         = 0;
  atlasEncoderParams.tileIdList.resize(2);
  atlasEncoderParams.tileIdList[0] = 0;
  atlasEncoderParams.tileIdList[1] = 1;
  // submeshIdsInTile[tile index][submesh index]
  atlasEncoderParams.submeshIdsInTile.resize(2);
  atlasEncoderParams.submeshIdsInTile[0].resize(1);
  atlasEncoderParams.submeshIdsInTile[1].resize(1);
  atlasEncoderParams.submeshIdsInTile[0][0]    = 0;
  atlasEncoderParams.submeshIdsInTile[1][0]    = 0;
  atlasEncoderParams.subdivisionIterationCount = subdivisionIterationCount;

  atlasEncoder.initializeAtlasParameterSets(atlasStream, atlasEncoderParams);

  // TODO: this should go to initializeAtlasParameterSets and should be parameter
  std::vector<int32_t> submeshIdtoIndex = {0};
  atlasEncoder.setsubmeshIdtoIndex(submeshIdtoIndex);

  std::pair<int32_t, int32_t>                geometryVideoSize;
  std::vector<std::pair<uint32_t, uint32_t>> attributeVideoSize;

  geometryVideoSize.first  = 256;
  geometryVideoSize.second = 640;

  attributeVideoSize.resize(1);

  attributeVideoSize[0].first  = 2048;
  attributeVideoSize[0].second = 4096;

  std::vector<std::vector<atlas::AtlasTile>> tileAreasInVideo;
  std::vector<std::vector<atlas::AtlasTile>> reconAtlasTiles;

  foo_tile_init(tileAreasInVideo, geometryVideoSize, attributeVideoSize);

  atlasEncoder.compressAtlas(atlasStream,
                             tileAreasInVideo,
                             reconAtlasTiles,
                             submeshes,
                             atlasEncoderParams,
                             geometryVideoSize,
                             attributeVideoSize);

  atlasWriter.encode(atlasStream, bitstream);

  if (!bitstream.save(compressedFilename)) {
    std::cerr << "Error: can't save serialized atlas bitstream!\n";
    return 1;
  }

  return 0;
}
