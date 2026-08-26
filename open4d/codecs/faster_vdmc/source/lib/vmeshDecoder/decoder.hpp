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

#pragma once

#include <cstdint>
#include <string>

#include "atlasTileStruct.hpp"
#include "atlasDecoder.hpp"
#include "vmc.hpp"
#include "v3cBitstream.hpp"
#include "motionContexts.hpp"
#include "entropy.hpp"
#include "acDisplacementFrame.hpp"

namespace vmesh {

//============================================================================

//============================================================================

struct VMCDecoderParameters {
  bool               checksum                     = true;
  std::string        textureVideoHDRToolDecConfig = {};
  int32_t textureVideoUpsampleFilter          = 0;
  int32_t textureVideoConversionThreadCount   = 0;
  bool    textureVideoFullRange               = false;
  bool    dequantizeUV                        = true;
  bool    keepIntermediateFiles               = false;
  bool               keepBaseMesh                 = false;
  bool               keepVideoFiles               = false;
  bool               reconstructNormals           = true;
  vmesh::ColourSpace outputAttributeColourSpace_  = ColourSpace::BGR444p;
  bool               processZipperingSEI          = true;
  int                targetLoD                    = -1;
  std::string        resultPath                   = {};
  std::string        HMMCTSExtractorPath          = {};
  int                targetLayer                  = 2;  // default: 2
  int                maxLayersMinus1              = 2;
};

class VMCDecoder {
public:
  VMCDecoder()                                 = default;
  VMCDecoder(const VMCDecoder& rhs)            = delete;
  VMCDecoder& operator=(const VMCDecoder& rhs) = delete;
  ~VMCDecoder()                                = default;

  int32_t decompress(V3cBitstream&               syntax,
                     const V3CParameterSet&      vps,
                     int32_t                     codedV3cSequenceCount,
                     Sequence&                   reconstruct,
                     const VMCDecoderParameters& params);

  inline void setKeepFilesPathPrefix(const std::string& path) {
    _keepFilesPathPrefix = path;
  }
  
#if CONFORMANCE_LOG_DEC
  void setLogger(Logger& logger) {
      logger_ = &logger;
      TRACE_DESCRIPTION("**********BITSTREAM DESCRIPTION**********\n (add description of conformance bitstream here)");
      TRACE_HLS("**********HIGH LEVEL SYNTAX**********\n");
      TRACE_ATLAS("**********ATLAS**********\n");
      TRACE_TILE("**********TILE**********\n");
      TRACE_MFRAME("**********MESH FRAME (before post-reconstruction)**********\n");
      TRACE_RECFRAME("**********RECONSTRUCTED MESH FRAME**********\n");
      TRACE_PICTURE("**********VIDEO BITSTREAMS**********\n");
      TRACE_BASEMESH("**********BASEMESH BITSTREAM**********\n");
      TRACE_DISPLACEMENT("**********DISPLACEMENT BISTREAM**********\n");
  }
  // see A.2.3.2 in ISO/IEC 23090-36
  void getHighLevelSyntaxLog(V3cBitstream& syntax);
  // see A.2.3.3 in ISO/IEC 23090-36
  void getPictureLog(FrameSequence<uint16_t> video);
  // see A.2.3.4 in ISO/IEC 23090-36
  void getBasemeshLog(
      std::vector<std::vector<VMCBasemesh>> basemeshes, 
      std::vector<uint32_t>& submeshIdtoInde);
  // see A.2.3.5 in ISO/IEC 23090-36
  void getACDisplacementLog(
      std::vector<std::vector<acdisplacement::AcDisplacementFrame>>& decDisplacements,
      std::vector<uint32_t> subDisplIDToIdx);
  // see A.2.3.7 in ISO/IEC 23090-36
  void getAtlasLog(
      int32_t                     frameIndex,
      std::vector<std::vector<AtlasTile>>& decodedTiles,
      V3cBitstream& syntax,
      const V3CParameterSet& vps);
  // see A.2.3.8 in ISO/IEC 23090-36
  void getTileLog(
      int32_t                     frameIndex,
      std::vector<std::vector<AtlasTile>>& decodedTiles,
      V3cBitstream& syntax);
  // see A.2.3.9 in ISO/IEC 23090-36
  void getMFrameLog(
      int32_t frameIdx,
      std::vector<VMCDecMesh> meshes);
  // see A.2.3.10 in ISO/IEC 23090-36
  void getRecFrameLog(int32_t        frameIndex,
      TriangleMesh<MeshType>& reconstructed);
  Logger* logger_ = nullptr;
#endif
private:
  bool separateDispTextureVideo(const V3CParameterSet&   vps,
                                int32_t                  atlasId,
                                FrameSequence<uint16_t>& decPacked,
                                FrameSequence<uint16_t>& dispVideo,
                                FrameSequence<uint16_t>& attrVideo16);
  bool videoConversion(FrameSequence<uint16_t>&    dec,
                       std::vector<size_t>&        videoBitdepth,
                       uint32_t                    sourceBitdepth,
                       vmesh::ColourSpace          videoColourSpace,
                       uint32_t                    videoWidth,
                       uint32_t                    videoHeight,
                       uint32_t                    sourceWidth,
                       uint32_t                    sourceHeight,
                       const VMCDecoderParameters& params);

  int32_t reconstructSubmesh(
    const V3CParameterSet&                            vps,
    int32_t                                           atlasId,
    const std::vector<AtlasSequenceParameterSetRbsp>& aspsList,
    const std::vector<AtlasFrameParameterSetRbsp>&    afpsList,
    const std::vector<std::vector<AtlasTile>>&        decodedTiles,
    int32_t                                           frameIndex,
    int32_t                                           tileIndex,
    int32_t                                           submeshIndexInAtlas,
    int32_t                                           submeshIndexInBaseMesh,
    VMCBasemesh&
      decodedSubmesh,  //const is dropped due to uvcoordinate generation
    const FrameSequence<uint16_t>&              dispVideo,
    const std::vector<std::vector<acdisplacement::AcDisplacementFrame>>& decodedAcDispls,
    uint8_t displacementCodedType,  //0.none 1.displ 2.video
    const VMCDecoderParameters& params,
    VMCDecMesh&                 recSubmesh);
  //==========================================================
  //[orthoAtlas] v4.0_integration : 0dc060ea8289149262d6ce1ac680577ca50c02e4
  int32_t
  reconstructBaseMeshUvCoordinates(TriangleMesh<MeshType>& base,
                                   int32_t                 submeshIndex,
                                   int32_t                 frameIndex,
                                   const V3CParameterSet&  vps,
                                   const AtlasSequenceParameterSetRbsp& asps,
                                   const AtlasFrameParameterSetRbsp&    afps,
                                   const AtlasPatch& decodedAtlasPatch);
  //==========================================================
  int32_t subdivideBaseMesh(TriangleMesh<MeshType>&         base,
                            int32_t                         frameIndex,
                            int32_t                         submeshIndex,
                            VMCDecMesh&                     vdmcOutputSubmesh,
                            std::vector<SubdivisionMethod>* subdivMethods,
                            const int32_t subdivisionIterationCount,
                            const bool    interpolateSubdividedNormalsFlag,
                            const int32_t edgeLengthThreshold          = 0,
                            const int32_t bitshiftEdgeBasedSubdivision = 0);
  void
       conversionBasemeshes(const V3CParameterSet&                 vps,
                            uint8_t                                atlasId,
                            std::vector<std::vector<VMCBasemesh>>& decodedMeshes);
  void adjustTextureCoordinates(const V3CParameterSet&  vps,
                                int32_t                 atlasId,
                                int32_t                 attributeIndex,
                                int32_t                 bitDepthTexCoord,
                                const AtlasTile&          decodedTile,
                                const AtlasPatch&         decodedPatch,
                                int32_t                 submeshPos,
                                int32_t                 frameIndex,
                                int32_t                 tileIndex,
                                TriangleMesh<MeshType>& submesh);
  void
       appendReconstructedSubmeshes(const V3CParameterSet&      vps,
                                    int32_t                     atlasIndex,
                                    int32_t                     frameIndex,
                                    std::vector<VMCDecMesh>&    submeshes,  //frame
                                    Sequence&                   reconstruct,
                                    const VMCDecoderParameters& params);
  void checkUVranges(int32_t                  fIdx,
                     int32_t                  submeshCount,
                     std::vector<VMCDecMesh>& decMesh);
  // function for zippering processing
  size_t
              calculateAFOCvalForZippering(AtlasBitstream&               amStream,
                                           std::vector<AtlasTileLayerRbsp>& atglList,
                                           size_t                           atglOrder);
  void        zippering_decode(AtlasBitstream&          adStream,
                               const VMCDecoderParameters& params);
  void        zippering_reconstruct(int32_t                  frameIdx,
                                    std::vector<VMCDecMesh>& submeshes,  //sequence,
                                    int32_t                  submeshCount,
                                    const VMCDecoderParameters& params);
  bool        LoDVideobitstreamExtraction(V3cBitstream&               syntax,
                                          const VMCDecoderParameters& params);
  void        setConsitantFourCCCode(const V3cBitstream& syntax);

  int         getCodedCodecId(const V3cBitstream&    syntax,
                              const V3CParameterSet& vps,
                              unsigned char          codecCodecId,
                              const bool             isVideo);
                              
  std::string _keepFilesPathPrefix = {};
  std::vector<uint32_t> submeshIDToIndexBM_;  //orange -> 0
  std::vector<uint32_t> submeshIndexToIDBM_;
  std::vector<uint32_t> submeshIDToIndexAD_;
  std::vector<uint32_t> displIDToIndex_;
  std::vector<uint32_t> tileIdtoIndex_;  //orange -> 0

  //*************
  //output of vdmc decoder
  //std::vector<std::vector<VMCDecMesh>> vdmcOutputFrame_; //[submesh][frameCount]
  //*************

  //zippering
  std::vector<int>                 applyZippering_;
  std::vector<size_t>              zipperingMatchMaxDistancePerFrame_;
  std::vector<std::vector<size_t>> zipperingMatchMaxDistancePerSubmesh_;
  std::vector<std::vector<std::vector<int64_t>>> zipperingDistanceBorderPoint_;
  std::vector<std::vector<std::vector<Vec2<size_t>>>>
                       zipperingMatchedBorderPoint_;
  std::vector<uint8_t> methodForUnmatchedLoDs_;
  std::vector<std::vector<std::vector<int64_t>>>
       zipperingMatchMaxDistancePerSubmeshPair_;
  bool zipperingLinearSegmentation_;
  std::vector<std::vector<std::vector<std::vector<size_t>>>> boundaryIndex_;
  std::vector<std::vector<size_t>>                           crackCount_;

  // LoD-based displacement video bitstream extraction
  bool                     LoDExtractionEnable = true;
  std::vector<std::string> consitantFourCCCode_;
};

//============================================================================

}  // namespace vmesh
