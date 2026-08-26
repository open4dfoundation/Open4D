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

#include "encoder.hpp"

#include <cmath>

#include <array>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <string.h>

#include "entropy.hpp"
#include "vmc.hpp"
#include "util/misc.hpp"
#include "util/mesh.hpp"

namespace vmesh {

void
VMCEncoder::calculateMaxPixelCountForDisplacementFrame(
  std::vector<std::vector<VMCSubmesh>>& encFrames,
  const VMCEncoderParameters&           encParams,
  std::vector<int32_t>&                 maxPixelCount) {
  auto submeshCount               = encFrames.size();
  auto frameCount0                = (int)encFrames[0].size();
  auto geometryVideoWidthInBlocks = encParams.geometryVideoWidthInBlocks;
  auto geometryVideoBlockSize = (1 << encParams.log2GeometryVideoBlockSize);
  const auto pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;

  maxPixelCount.resize(submeshCount, 0);
  for (size_t frameIndex = 0; frameIndex < frameCount0; frameIndex++) {
    for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        auto& dispRec     = encFrames[submeshIdx][frameIndex].disp;
        auto  vertexCount = (int32_t)dispRec.size();
        auto  numLods =
          encFrames[submeshIdx][0].subdivInfoLevelOfDetails.size();
        int32_t              lodExtraPixels     = 0;
        int                  prevLodStart       = 0;
        int32_t              extraPaddingBlocks = 0;
        std::vector<int32_t> numberBlocksPerLod;
        numberBlocksPerLod.resize(numLods);
        for (int i = 0; i < numLods; i++) {
          int lodCount = encFrames[submeshIdx][frameIndex]
                           .subdivInfoLevelOfDetails[i]
                           .pointCount
                         - prevLodStart;
          lodExtraPixels +=
            ((lodCount + pixelsPerBlock - 1) / pixelsPerBlock) * pixelsPerBlock
            - lodCount;
          prevLodStart = encFrames[submeshIdx][frameIndex]
                           .subdivInfoLevelOfDetails[i]
                           .pointCount;

          int32_t lodExtraPixelsPerLoD =
            std::floor((lodCount + pixelsPerBlock - 1) / pixelsPerBlock)
              * pixelsPerBlock
            - lodCount;
          numberBlocksPerLod[i] =
            (lodCount + lodExtraPixelsPerLoD) / pixelsPerBlock;
        }

        // calculate how many extra blocks is needed
        // LoD should occupy a rectangle region
        if (encParams.lodPatchesEnable == 1) {
          auto remainingBlocksInRow = geometryVideoWidthInBlocks;
          for (int i = 0; i < numLods; i++) {
            // first check if we start with full row
            // if yes then we just calculate the remaining rows for the next LoD
            if (remainingBlocksInRow == geometryVideoWidthInBlocks) {
              remainingBlocksInRow =
                remainingBlocksInRow
                - (numberBlocksPerLod[i] % geometryVideoWidthInBlocks);
            } else {
              // check if we can put all blocks of LoD in remaining Blocks
              if (numberBlocksPerLod[i] <= remainingBlocksInRow) {
                remainingBlocksInRow =
                  remainingBlocksInRow - numberBlocksPerLod[i];
                if (remainingBlocksInRow == 0) {
                  remainingBlocksInRow = geometryVideoWidthInBlocks;
                }
              } else {
                extraPaddingBlocks += remainingBlocksInRow;
                remainingBlocksInRow =
                  geometryVideoWidthInBlocks
                  - (numberBlocksPerLod[i] % geometryVideoWidthInBlocks);
                if (remainingBlocksInRow == 0) {
                  remainingBlocksInRow = geometryVideoWidthInBlocks;
                }
              }
            }
          }
          extraPaddingBlocks += remainingBlocksInRow;
        }
        int32_t vertexCountForFrame =
          vertexCount + lodExtraPixels + (extraPaddingBlocks * pixelsPerBlock);
        maxPixelCount[submeshIdx] =
          std::max(vertexCountForFrame, maxPixelCount[submeshIdx]);
    }
  }
}

void
VMCEncoder::calculateMaxBlocksPerSubmeshPerTile(
  std::vector<std::vector<VMCSubmesh>>& encFrames,
  const VMCEncoderParameters&           encParams,
  std::vector<int32_t>&                 maxPixelCount,
  std::vector<std::vector<int32_t>>&    maxBlockWidth,
  std::vector<std::vector<int32_t>>&    maxBlockHeight) {
  auto frameCount0            = (int)encFrames[0].size();
  auto geometryVideoBlockSize = (1 << encParams.log2GeometryVideoBlockSize);
  const auto pixelsPerBlock = geometryVideoBlockSize * geometryVideoBlockSize;
  maxBlockWidth.resize(encParams.numTilesGeometry);
  maxBlockHeight.resize(encParams.numTilesGeometry);

  for (uint32_t frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
    for (size_t tileIdx = 0; tileIdx < encParams.numTilesGeometry; tileIdx++) {
      auto submeshCount = encParams.submeshIdsInTile[tileIdx].size();
      maxBlockWidth[tileIdx].resize(submeshCount, 0);
      maxBlockHeight[tileIdx].resize(submeshCount, 0);
      for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        auto    submeshId = encParams.submeshIdsInTile[tileIdx][submeshIdx];
        int32_t tileWidth =
          tileAreasInVideo_[tileIdx][frameIdx].tileGeometryArea_.sizeX;
        int32_t tileWidthInBlocks = tileWidth / geometryVideoBlockSize;
        auto    blockCount        = std::max(
          1, (maxPixelCount[submeshId] + pixelsPerBlock - 1) / pixelsPerBlock);
        maxBlockHeight[tileIdx][submeshIdx] = std::max(
          1, (blockCount + tileWidthInBlocks - 1) / tileWidthInBlocks);
        if (((ColourSpace)encParams.displacementVideoChromaFormat
               == ColourSpace::YUV420p
             || (ColourSpace)encParams.displacementVideoChromaFormat
                  == ColourSpace::YUV422p
             || (ColourSpace)encParams.displacementVideoChromaFormat
                  == ColourSpace::YUV400p)
            && (!encParams.applyOneDimensionalDisplacement))
          maxBlockHeight[tileIdx][submeshIdx] *= 3;
        maxBlockWidth[tileIdx][submeshIdx] =
          std::min(blockCount, tileWidthInBlocks);
      }
    }
  }
}

void
VMCEncoder::setPatchAreaForAcDisplacement() {
  auto frameCount0 = (int)tileAreasInVideo_[0].size();
  for (uint32_t frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
    for (int tileIdx = 0; tileIdx < tileAreasInVideo_.size(); tileIdx++) {
      auto tile = tileAreasInVideo_[tileIdx][frameIdx];
      if (tile.tileType_ == I_TILE || tile.tileType_ == P_TILE
          || tile.tileType_ == SKIP_TILE) {
        tile.tileGeometryArea_.sizeX = 0;
        tile.tileGeometryArea_.sizeY = 0;
        tile.tileGeometryArea_.LTx   = 0;
        tile.tileGeometryArea_.LTy   = 0;
        for (size_t patchIdx = 0; patchIdx < tile.patches_.size();
             patchIdx++) {
          tile.patches_[patchIdx].geometryPatchArea.sizeX = 0;
          tile.patches_[patchIdx].geometryPatchArea.sizeY = 0;
          tile.patches_[patchIdx].geometryPatchArea.LTx   = 0;
          tile.patches_[patchIdx].geometryPatchArea.LTy   = 0;
        }
      }
    }
  }
}

void
VMCEncoder::setPatchAreaForVideoDisplacement(
  std::vector<std::vector<VMCSubmesh>>& encFrames,
  const VMCEncoderParameters&           encParams) {
  auto frameCount0 = (int)encFrames[0].size();
  auto geometryVideoBlockSize = (1 << encParams.log2GeometryVideoBlockSize);

  std::vector<int32_t>              maxVertextCount;
  std::vector<std::vector<int32_t>> maxBlockWidth;
  std::vector<std::vector<int32_t>> maxBlockHeight;

  calculateMaxPixelCountForDisplacementFrame(
    encFrames, encParams, maxVertextCount);
  calculateMaxBlocksPerSubmeshPerTile(
    encFrames, encParams, maxVertextCount, maxBlockWidth, maxBlockHeight);

  if (encParams.numTilesGeometry == 2 && encParams.numSubmesh == 3) {
    for (uint32_t frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
      tileAreasInVideo_[0][frameIdx].tileGeometryArea_.sizeX =
        std::max(maxBlockWidth[0][0], maxBlockWidth[0][1])
        * geometryVideoBlockSize;
      tileAreasInVideo_[1][frameIdx].tileGeometryArea_.sizeX =
        (maxBlockWidth[1][0]) * geometryVideoBlockSize;

      tileAreasInVideo_[0][frameIdx].tileGeometryArea_.sizeY =
        (maxBlockHeight[0][0] + maxBlockHeight[0][1]) * geometryVideoBlockSize;
      tileAreasInVideo_[1][frameIdx].tileGeometryArea_.sizeY =
        (maxBlockHeight[1][0]) * geometryVideoBlockSize;
      tileAreasInVideo_[1][frameIdx].tileGeometryArea_.LTy =
        tileAreasInVideo_[0][frameIdx].tileGeometryArea_.sizeY;
      geometryVideoSize.first =
        encParams.geometryVideoWidthInBlocks * geometryVideoBlockSize;
      geometryVideoSize.second =
        tileAreasInVideo_[0][frameIdx].tileGeometryArea_.sizeY
        + tileAreasInVideo_[1][frameIdx].tileGeometryArea_.sizeY;

      // set video height : stack!
      tileAreasInVideo_[0][frameIdx].patches_[0].geometryPatchArea.sizeX =
        tileAreasInVideo_[0][frameIdx].tileGeometryArea_.sizeX;
      tileAreasInVideo_[0][frameIdx].patches_[0].geometryPatchArea.sizeY =
        maxBlockHeight[0][0] * geometryVideoBlockSize;
      tileAreasInVideo_[0][frameIdx].patches_[1].geometryPatchArea.sizeX =
        tileAreasInVideo_[0][frameIdx].tileGeometryArea_.sizeX;
      tileAreasInVideo_[0][frameIdx].patches_[1].geometryPatchArea.sizeY =
        maxBlockHeight[0][1] * geometryVideoBlockSize;
      tileAreasInVideo_[1][frameIdx].patches_[0].geometryPatchArea.sizeX =
        tileAreasInVideo_[1][frameIdx].tileGeometryArea_.sizeX;
      tileAreasInVideo_[1][frameIdx].patches_[0].geometryPatchArea.sizeY =
        maxBlockHeight[1][0] * geometryVideoBlockSize;
    }
    std::vector<std::vector<int32_t>> submeshStartBlockY(
      encParams.numTilesGeometry);
    for (int tileIdx = 0; tileIdx < encParams.numTilesGeometry; tileIdx++) {
      auto submeshCountInTile = encParams.submeshIdsInTile[tileIdx].size();
      auto heightDispVideo    = 0;
      submeshStartBlockY[tileIdx].resize(submeshCountInTile, 0);
      for (int submeshIdxInTile = 0; submeshIdxInTile < submeshCountInTile;
           submeshIdxInTile++) {
        submeshStartBlockY[tileIdx][submeshIdxInTile] = heightDispVideo;
        heightDispVideo +=
          maxBlockHeight[tileIdx][submeshIdxInTile] * geometryVideoBlockSize;
      }
    }

    //set patch width, height, and start position
    for (uint32_t frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
      for (int tileIdx = 0; tileIdx < encParams.numTilesGeometry; tileIdx++) {
        size_t patchIdxStart      = 0;
        auto   submeshCountInTile = encParams.submeshIdsInTile[tileIdx].size();
        for (size_t submeshIdxInTile = 0;
             submeshIdxInTile < submeshCountInTile;
             submeshIdxInTile++) {
          auto submeshId =
            encParams.submeshIdsInTile[tileIdx][submeshIdxInTile];
          auto submeshIdx            = submeshIdtoIndex_[submeshId];
          auto patchesInSubmeshCount = 1;
          auto lodCount =
            encFrames[submeshIdx][0].subdivInfoLevelOfDetails.size();
          if (encParams.lodPatchesEnable == 1) {
            patchesInSubmeshCount = lodCount;
          }

          std::vector<int32_t> vertexCountPerLod(lodCount);
          for (size_t lod = 0; lod < lodCount; lod++) {
            vertexCountPerLod[lod] = encFrames[submeshIdx][frameIdx]
                                       .subdivInfoLevelOfDetails[lod]
                                       .pointCount;
            if (lod != 0)
              vertexCountPerLod[lod] -= encFrames[submeshIdx][frameIdx]
                                          .subdivInfoLevelOfDetails[lod - 1]
                                          .pointCount;
          }

          int32_t              blockCount = 0;
          std::vector<lodInfo> lodVertexInfo;
          int32_t              geometryVideoBlockSize =
            (1 << encParams.log2GeometryVideoBlockSize);
          uint32_t geometryPatchWidthInBlocks =
            (uint32_t)std::ceil((double)tileAreasInVideo_[tileIdx][frameIdx]
                                  .tileGeometryArea_.sizeX
                                / (double)geometryVideoBlockSize);

          uint32_t geometryPatchHeightInBlocks =
            (uint32_t)std::ceil((double)tileAreasInVideo_[tileIdx][frameIdx]
                                  .tileGeometryArea_.sizeY
                                / (double)geometryVideoBlockSize);
          const uint32_t pointCount =
            encFrames[submeshIdx][frameIdx].disp.size();
          blockCount = getLodVertexInfo(vertexCountPerLod,
                                        lodVertexInfo,
                                        pointCount,
                                        geometryVideoBlockSize,
                                        geometryPatchWidthInBlocks,
                                        encParams.lodPatchesEnable);

          for (size_t submeshPatchIdx = 0;
               submeshPatchIdx < patchesInSubmeshCount;
               submeshPatchIdx++) {
            size_t patchIdx = patchIdxStart + submeshPatchIdx;

            auto& patchGeometryArea = tileAreasInVideo_[tileIdx][frameIdx]
                                        .patches_[patchIdx]
                                        .geometryPatchArea;
            uint32_t geoPatchSizeY =
              ((lodVertexInfo[submeshPatchIdx].blockCount - 1)
               / geometryPatchWidthInBlocks)
              + 1;
            uint32_t geoPatchSizeX =
              (geoPatchSizeY > 1) ? geometryPatchWidthInBlocks
                                  : lodVertexInfo[submeshPatchIdx].blockCount;
            uint32_t geoPatchLTy =
              ((lodVertexInfo[submeshPatchIdx].startBlockIndex)
               / geometryPatchWidthInBlocks);
            uint32_t geoPatchLTx =
              ((lodVertexInfo[submeshPatchIdx].startBlockIndex)
               % geometryPatchWidthInBlocks);

            if (encParams.displacementReversePacking) {
              geoPatchLTy = geometryPatchHeightInBlocks - geoPatchLTy
                            - geoPatchSizeY
                            - (submeshStartBlockY[tileIdx][submeshIdxInTile]
                               / geometryVideoBlockSize);
              geoPatchLTx =
                geometryPatchWidthInBlocks - geoPatchLTx - geoPatchSizeX;
            }

            if (encParams.lodPatchesEnable == 1) {
              patchGeometryArea.sizeX = geoPatchSizeX * geometryVideoBlockSize;
              patchGeometryArea.sizeY = geoPatchSizeY * geometryVideoBlockSize;
              patchGeometryArea.LTy   = geoPatchLTy * geometryVideoBlockSize;
              patchGeometryArea.LTx   = geoPatchLTx * geometryVideoBlockSize;
              tileAreasInVideo_[tileIdx][frameIdx].patches_[patchIdx].lodIdx_ =
                submeshPatchIdx;
            } else {
              patchGeometryArea.sizeX =
                (maxBlockWidth[tileIdx][patchIdx] * geometryVideoBlockSize);
              patchGeometryArea.sizeY =
                (maxBlockHeight[tileIdx][patchIdx] * geometryVideoBlockSize);
              patchGeometryArea.LTx = 0;
              patchGeometryArea.LTy =
                submeshStartBlockY[tileIdx][submeshIdxInTile];
              tileAreasInVideo_[tileIdx][frameIdx].patches_[patchIdx].lodIdx_ =
                0;
            }
          }  //patchIdx
          patchIdxStart = patchIdxStart + patchesInSubmeshCount;
        }  //submeshIdx
      }    //tileIdx
    }      //frameIdx
  } else if (encParams.numTilesGeometry == 1) {
    std::vector<std::vector<int32_t>> patchStartBlockY(
      encParams.numTilesGeometry);

    auto heightDispVideo = 0;
    for (int tileIdx = 0; tileIdx < encParams.numTilesGeometry; tileIdx++) {
      auto submeshCount = encParams.submeshIdsInTile[tileIdx].size();

      patchStartBlockY[tileIdx].resize(submeshCount, 0);
      for (int submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
        patchStartBlockY[tileIdx][submeshIdx] = heightDispVideo;
        heightDispVideo +=
          maxBlockHeight[tileIdx][submeshIdx] * geometryVideoBlockSize;
      }
    }

    auto widthDispVideo =
      encParams.geometryVideoWidthInBlocks * geometryVideoBlockSize;
    geometryVideoSize.first  = widthDispVideo;
    geometryVideoSize.second = heightDispVideo;

    //set patch width, height, and start position
    for (uint32_t frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
      for (int tileIdx = 0; tileIdx < encParams.numTilesGeometry; tileIdx++) {
        size_t patchIdxStart = 0;
        auto   submeshCount  = encParams.submeshIdsInTile[tileIdx].size();
        tileAreasInVideo_[tileIdx][frameIdx].tileGeometryArea_.LTx = 0;
        tileAreasInVideo_[tileIdx][frameIdx].tileGeometryArea_.LTy = 0;
        tileAreasInVideo_[tileIdx][frameIdx].tileGeometryArea_.sizeX =
          geometryVideoSize.first;
        tileAreasInVideo_[tileIdx][frameIdx].tileGeometryArea_.sizeY =
          geometryVideoSize.second;
        for (size_t submeshIdx = 0; submeshIdx < submeshCount; submeshIdx++) {
            auto patchesInSubmeshCount = 1;
            auto lodCount =
              encFrames[submeshIdx][0].subdivInfoLevelOfDetails.size();
            if (encParams.lodPatchesEnable == 1) {
              patchesInSubmeshCount = lodCount;
            }

            std::vector<int32_t> vertexCountPerLod(lodCount);
            for (size_t lod = 0; lod < lodCount; lod++) {
              vertexCountPerLod[lod] = encFrames[submeshIdx][frameIdx]
                                         .subdivInfoLevelOfDetails[lod]
                                         .pointCount;
              if (lod != 0)
                vertexCountPerLod[lod] -= encFrames[submeshIdx][frameIdx]
                                            .subdivInfoLevelOfDetails[lod - 1]
                                            .pointCount;
            }

            int32_t              blockCount = 0;
            std::vector<lodInfo> lodVertexInfo;
            int32_t              geometryVideoBlockSize =
              (1 << encParams.log2GeometryVideoBlockSize);
            uint32_t geometryPatchWidthInBlocks =
              (uint32_t)std::ceil((double)geometryVideoSize.first
                                  / (double)geometryVideoBlockSize);
            uint32_t geometryPatchHeightInBlocks =
              (uint32_t)std::ceil((double)geometryVideoSize.second
                                  / (double)geometryVideoBlockSize);
            const uint32_t pointCount =
              encFrames[submeshIdx][frameIdx].disp.size();
            blockCount = getLodVertexInfo(vertexCountPerLod,
                                          lodVertexInfo,
                                          pointCount,
                                          geometryVideoBlockSize,
                                          geometryPatchWidthInBlocks,
                                          encParams.lodPatchesEnable);

            // TODO assume one tile one submesh
            for (size_t submeshPatchIdx = 0;
                 submeshPatchIdx < patchesInSubmeshCount;
                 submeshPatchIdx++) {
              size_t patchIdx = patchIdxStart + submeshPatchIdx;

              auto& patchGeometryArea = tileAreasInVideo_[tileIdx][frameIdx]
                                          .patches_[patchIdx]
                                          .geometryPatchArea;
              uint32_t geoPatchSizeY =
                ((lodVertexInfo[patchIdx].blockCount - 1)
                 / geometryPatchWidthInBlocks)
                + 1;
              uint32_t geoPatchSizeX = (geoPatchSizeY > 1)
                                         ? geometryPatchWidthInBlocks
                                         : lodVertexInfo[patchIdx].blockCount;
              uint32_t geoPatchLTy = ((lodVertexInfo[patchIdx].startBlockIndex)
                                      / geometryPatchWidthInBlocks);
              uint32_t geoPatchLTx = ((lodVertexInfo[patchIdx].startBlockIndex)
                                      % geometryPatchWidthInBlocks);

              if (encParams.displacementReversePacking) {
                geoPatchLTy =
                  geometryPatchHeightInBlocks - geoPatchLTy - geoPatchSizeY;
                geoPatchLTx =
                  geometryPatchWidthInBlocks - geoPatchLTx - geoPatchSizeX;
              }

              if (encParams.lodPatchesEnable == 1) {
                patchGeometryArea.sizeX =
                  geoPatchSizeX * geometryVideoBlockSize;
                patchGeometryArea.sizeY =
                  geoPatchSizeY * geometryVideoBlockSize;
                patchGeometryArea.LTy = geoPatchLTy * geometryVideoBlockSize;
                patchGeometryArea.LTx = geoPatchLTx * geometryVideoBlockSize;
                tileAreasInVideo_[tileIdx][frameIdx]
                  .patches_[patchIdx]
                  .lodIdx_ = patchIdx;
              } else {
                patchGeometryArea.sizeX =
                  (maxBlockWidth[tileIdx][patchIdx] * geometryVideoBlockSize);
                patchGeometryArea.sizeY =
                  (maxBlockHeight[tileIdx][patchIdx] * geometryVideoBlockSize);
                patchGeometryArea.LTx = 0;
                patchGeometryArea.LTy = patchStartBlockY[tileIdx][patchIdx];
                tileAreasInVideo_[tileIdx][frameIdx]
                  .patches_[patchIdx]
                  .lodIdx_ = 0;
              }
            }  //patchIdx
            patchIdxStart = patchIdxStart + patchesInSubmeshCount;
        }  //submeshIdx
      }    //tileIdx
    }      // frameidx
  } else {
    printf("tileCountGeometry %d\tsubmeshCount %d is not implemented\n",
           encParams.numTilesGeometry,
           encParams.numSubmesh);
  }
}
//============================================================================
void
VMCEncoder::setPatchAreaForDisplacements(
  std::vector<std::vector<VMCSubmesh>>& encFrames,
  const VMCEncoderParameters&           encParams) {
  if (encParams.encodeDisplacementType != 2) {
    setPatchAreaForAcDisplacement();
  } else {
    setPatchAreaForVideoDisplacement(encFrames, encParams);
  }

  auto frameCount0 = (int)tileAreasInVideo_[0].size();

  std::cout << "***place Displacement---------------------------\n";
  for (uint32_t frameIdx = 0; frameIdx < frameCount0; frameIdx++) {
    for (int tileIdx = 0; tileIdx < tileAreasInVideo_.size(); tileIdx++) {
      auto& tile = tileAreasInVideo_[tileIdx][frameIdx];
      if (tile.tileType_ == I_TILE || tile.tileType_ == P_TILE
          || tile.tileType_ == SKIP_TILE) {
        tile.printInfo();
      }
    }
  }

  std::cout << "\tGeometry video size : " << geometryVideoSize.first << " x "
            << geometryVideoSize.second << "\n";
}

}  // namespace vmesh
