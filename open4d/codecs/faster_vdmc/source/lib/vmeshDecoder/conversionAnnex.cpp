/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2022, ISO/IEC
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

#include "decoder.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>
#include "vmc.hpp"
#include "colourConverter.hpp"
#include "util/checksum.hpp"

using namespace vmesh;
void
VMCDecoder::conversionBasemeshes(
  const V3CParameterSet&                 vps,
  uint8_t                                atlasIdx,
  std::vector<std::vector<VMCBasemesh>>& decodedMeshes) {
  // do remapping and bit depth conversion from annex B
  auto& vpsExt = vps.getVpsVdmcExtension();
  //auto atlasIdx = 0;

  //Basemesh bitdepth scaling
  size_t bitDepthPosition =
    vpsExt.getVpsExtMeshGeo3dBitdepthMinus1(atlasIdx) + 1;
  for (int frameIdx = 0; frameIdx < decodedMeshes.size(); frameIdx++) {
    for (int submeshIdx = 0; submeshIdx < decodedMeshes[frameIdx].size();
         submeshIdx++) {
      auto& base       = decodedMeshes[frameIdx][submeshIdx].base;
      auto  qpPosition = decodedMeshes[frameIdx][submeshIdx].positionBitdepth;
      const auto scalePosition =
        ((1 << qpPosition) - 1.0) / ((1 << bitDepthPosition) - 1.0);
      const auto iscalePosition = 1.0 / scalePosition;
      for (int32_t v = 0, vcount = base.pointCount(); v < vcount; ++v) {
        base.setPoint(v, base.point(v) * iscalePosition);
      }
    }
  }

  if (vpsExt.getVpsExtMeshDataAttributeCount(atlasIdx) > 0) {
    for (int frameIdx = 0; frameIdx < decodedMeshes.size(); frameIdx++) {
      for (int submeshIdx = 0; submeshIdx < decodedMeshes[frameIdx].size();
           submeshIdx++) {
        auto& base = decodedMeshes[frameIdx][submeshIdx].base;
        // attribute mapping
        for (int attrIdx = 0;
             attrIdx
             < vps.getVpsVdmcExtension().getVpsExtMeshDataAttributeCount(
               atlasIdx);
             attrIdx++) {
          int bmAttrIdx =
            vpsExt.getVpsExtMeshAttributeIndex(atlasIdx, attrIdx);
          switch (vpsExt.getVpsExtMeshAttributeType(atlasIdx, attrIdx)) {
          case basemesh::BASEMESH_ATTRIBUTE_MATERIALID: {
            //TODO: clarification needed
            //int bmAttrType = bmsps.getBmspsMeshAttributeTypeId(bmAttrIdx);
            //if (bmAttrType == (int)ATTRIBUTE_FACEGROUPID) {
            //copy the attribute from face id to material idx
            base.materialIdxs() = base.faceIds();
            base.faceIds().clear();
            //}
            // create the material file and add the list of texture URLs
            auto listOfIndices = base.materialIdxs();
            std::sort(listOfIndices.begin(), listOfIndices.end());
            listOfIndices.erase(
              unique(listOfIndices.begin(), listOfIndices.end()),
              listOfIndices.end());
            int textureCount = listOfIndices.size();
            int maxTexIdx =
              *std::max_element(listOfIndices.begin(), listOfIndices.end());
            base.materialNames().resize(maxTexIdx + 1);
            base.textureMapUrls().resize(maxTexIdx + 1);
            for (int matIdx = 0; matIdx < maxTexIdx + 1; matIdx++) {
              base.materialNames()[matIdx] =
                "material_" + std::to_string(matIdx);
              // check if the index is used
              if (std::find(listOfIndices.begin(), listOfIndices.end(), matIdx)
                  == listOfIndices.end()) {
                base.textureMapUrls()[matIdx] = "";
              } else {
                base.textureMapUrls()[matIdx] =
                  "texture_" + std::to_string(matIdx) + ".png";
              }
            }
            break;
          }
          case basemesh::BASEMESH_ATTRIBUTE_NORMAL: {
              size_t bitDepthNormal = vpsExt.getVpsExtMeshAttributeBitdepthMinus1(atlasIdx, attrIdx)  + 1;
              if (base.normalCount()>0){
                  auto qpNormal = decodedMeshes[frameIdx][submeshIdx].normalBitdepth;
                  if(bitDepthNormal != qpNormal){
                    const auto scaleNormal =  ((1 << qpNormal) - 1.0) / ((1 << bitDepthNormal) - 1.0);
                    const auto iscaleNormal = 1.0 / scaleNormal;
                    for (int32_t n = 0, ncount = base.normalCount(); n < ncount; ++n) {
                        base.setNormal(n, base.normal(n) * iscaleNormal);
                    }
                  }
              }
          }
        }} // attrIdx
      }  //submeshIdx
    }    //frameIdx
  }      //if
}

bool
VMCDecoder::videoConversion(FrameSequence<uint16_t>&    dec,
                            std::vector<size_t>&        videoBitdepth,
                            uint32_t                    sourceBitdepth,
                            vmesh::ColourSpace          videoColourSpace,
                            uint32_t                    videoWidth,
                            uint32_t                    videoHeight,
                            uint32_t                    sourceWidth,
                            uint32_t                    sourceHeight,
                            const VMCDecoderParameters& params) {
  //conversion
  if (
    (videoColourSpace != ColourSpace::BGR444p
     && videoColourSpace != ColourSpace::GBR444p
     && videoColourSpace != ColourSpace::RGB444p)
    // [om] no 444 to 444 10 to 8 conversion, breaks C0 - modify Attribute2dBitdepthMinus1 for lossless ?
    // extending conversion should not be required as there is no conversion to perform there
    || (sourceBitdepth != videoBitdepth[0])) {
    // Convert video
    printf("Convert Texture video \n");
    fflush(stdout);
#if USE_HDRTOOLS
    auto convert = ColourConverter<uint16_t>::create(1);
    convert->initialize(params.textureVideoHDRToolDecConfig);
    convert->convert(dec);
#else
    auto convert = ColourConverter<uint16_t>::create(0);
    //"YUV420p_BGR444p_10_8_"
    auto mode = toString(videoColourSpace) + "_"
                + toString(params.outputAttributeColourSpace_) + "_"
                + std::to_string(videoBitdepth[0]) + "_"
                + std::to_string(sourceBitdepth) + "_"
                + std::to_string(params.textureVideoUpsampleFilter) + "_"
                + std::to_string(params.textureVideoFullRange);
    std::cout << "convert mode: " << mode << "\n";
    convert->initialize(mode);
    int32_t workerCount = params.textureVideoConversionThreadCount;
    if (workerCount == 0) {
      constexpr int32_t kAutomaticWorkerLimit = 8;
      workerCount = std::min(
        kAutomaticWorkerLimit,
        std::max(1, int32_t(std::thread::hardware_concurrency())));
    }
    workerCount =
      std::max(1, std::min(workerCount, std::max(1, dec.frameCount())));
    if (workerCount == 1) {
      convert->convert(dec);
    } else {
      // Each worker owns a frame; conversion order and arithmetic within a
      // frame are unchanged from the serial implementation.
      std::atomic<int32_t> nextFrame(0);
      const auto worker = [&]() {
        for (;;) {
          const auto frameIndex =
            nextFrame.fetch_add(1, std::memory_order_relaxed);
          if (frameIndex >= dec.frameCount()) { break; }
          convert->convert(dec[frameIndex]);
        }
      };
      std::vector<std::thread> workers;
      workers.reserve(workerCount - 1);
      for (int32_t i = 1; i < workerCount; ++i) {
        workers.emplace_back(worker);
      }
      worker();
      for (auto& thread : workers) { thread.join(); }
      dec.colourSpace() = params.outputAttributeColourSpace_;
    }
#endif
  }
  // Resolution conversion
  if ((videoWidth != sourceWidth) || (videoHeight != sourceHeight)) {
    // Annex B. resolution conversion
    printf("resolution conversion: decoded frame width, height: %d, %d "
           "nominal width, height: %d, %d",
           videoWidth,
           videoHeight,
           sourceWidth,
           sourceHeight);
    auto decN = dec;
    decN.resize(sourceWidth,
                sourceHeight,
                params.outputAttributeColourSpace_,
                dec.frameCount());
    float scaleY = (float)videoHeight / (float)sourceHeight;
    float scaleX = (float)videoWidth / (float)sourceWidth;
    std::cout << "scaleY " << scaleY << "scaleX" << scaleX << std::endl;
    for (int i = 0; i < dec.frameCount(); i++)
      for (int y = 0; y < sourceHeight; y++)
        for (int x = 0; x < sourceWidth; x++)
          for (int d = 0; d < 3; d++) {
            decN.frame(i).plane(d).set(
              y,
              x,
              dec.frame(i).plane(d).get(floor(y * scaleY), floor(x * scaleX)));
          }
    dec = decN;
  }
  return true;
}
