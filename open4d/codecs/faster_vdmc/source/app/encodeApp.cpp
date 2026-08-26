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

#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "encodeParams.h"
#include "version.hpp"
#include "sequenceInfo.hpp"
#include "v3cWriter.hpp"
#include "v3cReader.hpp"
#include "bitstreamStat.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace {

std::vector<std::string> commandLineArguments;

std::string
parallelPartPath(const std::string& outputPath, int gofIndex) {
  return outputPath + ".gof-" + std::to_string(gofIndex) + ".part.vmesh";
}

bool
mergeParallelOutputs(const Parameters&                 params,
                     const std::vector<std::string>& partPaths) {
  vmesh::SampleStreamV3CUnit merged;
  vmesh::Checksum mergedChecksum(params.encParams.videoAttributeCount);

  for (const auto& partPath : partPaths) {
    vmesh::Bitstream input;
    if (!input.load(partPath)) {
      std::cerr << "Error: cannot load parallel GOF output " << partPath
                << "\n";
      return false;
    }
    vmesh::SampleStreamV3CUnit part;
    vmesh::V3CReader reader;
    reader.read(input, part);
    auto& destination = merged.getV3CUnit();
    auto& source      = part.getV3CUnit();
    destination.insert(destination.end(),
                       std::make_move_iterator(source.begin()),
                       std::make_move_iterator(source.end()));

    if (params.checksum) {
      vmesh::Checksum partChecksum;
      const auto checksumPath =
        vmesh::removeExtension(partPath) + ".checksum";
      if (!partChecksum.read(checksumPath)) {
        std::cerr << "Error: cannot load parallel GOF checksum "
                  << checksumPath << "\n";
        return false;
      }
      for (const auto& value : partChecksum.checksumMesh())
        mergedChecksum.checksumMesh().push_back(value);
      if (mergedChecksum.checksumAtts().size()
          < partChecksum.checksumAtts().size())
        mergedChecksum.checksumAtts().resize(
          partChecksum.checksumAtts().size());
      for (size_t attribute = 0;
           attribute < partChecksum.checksumAtts().size();
           ++attribute) {
        for (const auto& value : partChecksum.checksumAtt(attribute))
          mergedChecksum.checksumAtt(attribute).push_back(value);
      }
    }
  }

  vmesh::V3CWriter writer;
  vmesh::Bitstream output;
  writer.write(
    merged, output, params.encParams.forceSsvhUnitSizePrecision);
  if (!output.save(params.compressedStreamPath)) {
    std::cerr << "Error: cannot save merged parallel bitstream\n";
    return false;
  }
  if (params.checksum
      && !mergedChecksum.write(
        vmesh::removeExtension(params.compressedStreamPath) + ".checksum")) {
    std::cerr << "Error: cannot save merged parallel checksum\n";
    return false;
  }

  for (const auto& partPath : partPaths) {
    std::remove(partPath.c_str());
    if (params.checksum)
      std::remove(
        (vmesh::removeExtension(partPath) + ".checksum").c_str());
  }
  return true;
}

#if defined(__unix__) || defined(__APPLE__)
bool
compressParallelGofs(const Parameters&          params,
                     vmesh::SequenceInfo&       sequenceInfo) {
  struct Child {
    pid_t pid;
    int   gofIndex;
  };

  std::vector<std::string> partPaths(sequenceInfo.gofCount());
  std::vector<Child>       active;
  bool                     success = true;
  auto                     gof     = sequenceInfo.begin();

  while (gof != sequenceInfo.end() || !active.empty()) {
    while (gof != sequenceInfo.end()
           && active.size() < static_cast<size_t>(params.gofParallelism)) {
      const int gofIndex = gof->index_;
      auto&     partPath = partPaths[gofIndex - params.gofIndexOffset];
      partPath = parallelPartPath(params.compressedStreamPath, gofIndex);

      std::vector<std::string> arguments = commandLineArguments;
      arguments.push_back("--gofParallelism=1");
      arguments.push_back("--gofIndexOffset=" + std::to_string(gofIndex));
      arguments.push_back("--startFrameIndex="
                          + std::to_string(gof->startFrameIndex_));
      arguments.push_back("--frameCount=" + std::to_string(gof->frameCount_));
      arguments.push_back("--gofMaxSize=" + std::to_string(gof->frameCount_));
      arguments.push_back("--textureTransferThreadCount="
                          + std::to_string(params.gofWorkerTextureThreads));
      arguments.push_back("--compressed=" + partPath);

      std::vector<char*> argv;
      argv.reserve(arguments.size() + 1);
      for (auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
      argv.push_back(nullptr);

      pid_t pid = -1;
      const int result = posix_spawnp(
        &pid, argv[0], nullptr, nullptr, argv.data(), environ);
      if (result != 0) {
        std::cerr << "Error: cannot launch GOF " << gofIndex << " worker\n";
        return false;
      }
      std::cout << "Started GOF " << gofIndex << " worker pid " << pid
                << "\n";
      active.push_back({pid, gofIndex});
      ++gof;
    }

    int   status      = 0;
    pid_t finishedPid = waitpid(-1, &status, 0);
    auto  child = std::find_if(active.begin(), active.end(), [&](const auto& c) {
      return c.pid == finishedPid;
    });
    if (child == active.end()) {
      std::cerr << "Error: received an unknown GOF worker status\n";
      return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      std::cerr << "Error: GOF " << child->gofIndex << " worker failed\n";
      success = false;
    } else {
      std::cout << "Finished GOF " << child->gofIndex << "\n";
    }
    active.erase(child);
  }

  return success && mergeParallelOutputs(params, partPaths);
}
#endif

}  // namespace

bool
compress(const Parameters& params) {
  const auto basePath = vmesh::removeExtension(params.compressedStreamPath);
  const bool computeMetrics = params.metParams.computePcc
                              || params.metParams.computeIbsm
                              || params.metParams.computePcqm;
  vmesh::SampleStreamV3CUnit ssvu;
  vmesh::BitstreamStat       bitstreamStat;
  vmesh::Checksum            checksum (params.encParams.videoAttributeCount);
  vmesh::VMCMetrics          metrics;
#if defined(BITSTREAM_TRACE)
  vmesh::Logger loggerHls;
  loggerHls.initilalize(basePath + "_hls", true);
#endif
  int32_t frameTobeEncoded = params.frameCount;
  size_t  lastSeparatorPos = params.compressedStreamPath.find_last_of("\\/");
  std::string parentPath =
    params.compressedStreamPath.substr(0, lastSeparatorPos);
  std::vector<std::string> sourceSubmeshPath;
  if (params.encParams.segmentByBaseMesh) {
    std::string submeshPrefix;
    if (params.useSubmeshFolder) {
      submeshPrefix = parentPath + "/";
      if (!vmesh::exist(submeshPrefix)) {
        std::cerr << "submesh input/output directory does not exist\n";
        return false;
      }
    } else submeshPrefix = "";
    sourceSubmeshPath = vmesh::VMCEncoder::makeSubmeshPaths(
      params.inputMeshPath, submeshPrefix, params.encParams.numSubmesh);
  } else if (params.encParams.numSubmesh > 1) {
    bool              submeshLoadOnly = false;
    vmesh::VMCEncoder encoder(params.encParams.videoAttributeCount);
    std::string       submeshPrefix;
    if (params.useSubmeshFolder) {
      submeshPrefix = parentPath + "/";
      if (!vmesh::exist(submeshPrefix)) {
        std::cerr << "submesh input/output directory does not exist\n";
        return false;
      }
    } else submeshPrefix = "";
    int32_t segmented = -1;
    if (params.encParams.submeshSegmentationType == 0 && params.encParams.numSubmesh == 2) {
      segmented =
        encoder.submeshSegmentByTriangleDensity(params.inputMeshPath,
                                                sourceSubmeshPath,
                                                params.encParams.numSubmesh,
                                                params.startFrame,
                                                params.frameCount,
                                                params.encParams,
                                                submeshPrefix,
                                                submeshLoadOnly);
      // check if the segmentation makes sense
      if (segmented == 0) {
          std::cout << "Segmentation by triangle density failed, trying the segmentation by 3D space" << std::endl;
          segmented = encoder.submeshSegment(params.inputMeshPath,
              sourceSubmeshPath,
              params.encParams.numSubmesh,
              params.startFrame,
              params.frameCount,
              params.encParams,
              submeshPrefix,
              submeshLoadOnly);

      }
    } else if(params.encParams.submeshSegmentationType == 0 ){
      segmented = encoder.submeshSegment(params.inputMeshPath,
                                         sourceSubmeshPath,
                                         params.encParams.numSubmesh,
                                         params.startFrame,
                                         params.frameCount,
                                         params.encParams,
                                         submeshPrefix,
                                         submeshLoadOnly);
    }
    if (segmented < 0) {
      std::cout << "submesh files open error\n";
      return false;
    } else if (segmented <= params.frameCount) {
      frameTobeEncoded = segmented;
      std::cout << "total available input files : " << frameTobeEncoded
                << "\n";
    } else {
      std::cout << "UNKNOWN ERROR : %d\n", segmented;
      return false;
    }
  }

  // Generate gof structure
  vmesh::SequenceInfo sequenceInfo;
  if (params.encParams.numSubmesh == 1 || params.encParams.segmentByBaseMesh) {
    sequenceInfo.generate(params.frameCount,
                          params.startFrame,
                          params.encParams.groupOfFramesMaxSize,
                          params.encParams.analyzeGof,
                          params.encParams.basemeshGOPList,
                          params.inputMeshPath);
    if (params.encParams.analyzeGof && params.encParams.basemeshGOPSize > 0) {
      sequenceInfo.applyGOP(params.encParams.basemeshGOPList,
                            params.inputMeshPath);
    }
  }
  else {
    sequenceInfo.generate(frameTobeEncoded,
                          params.startFrame,
                          params.encParams.groupOfFramesMaxSize);
  }

  for (auto& gofInfo : sequenceInfo) gofInfo.index_ += params.gofIndexOffset;

  if (params.gofParallelism > 1 && sequenceInfo.gofCount() > 1) {
#if defined(__unix__) || defined(__APPLE__)
    if (computeMetrics) {
      std::cerr << "Error: GOF parallelism does not support aggregate metrics; "
                   "run metrics after decoding\n";
      return false;
    }
    return compressParallelGofs(params, sequenceInfo);
#else
    std::cerr << "Error: GOF process parallelism is only available on POSIX\n";
    return false;
#endif
  }

  if (params.encParams.keepIntermediateFiles)
    sequenceInfo.save(basePath + "_gof.txt");

  //NOTE: atlasCount==1 is allowed
  int32_t atlasCount  = 1;
  int32_t atlasId     = 0;
  int32_t frameOffset = 0;
  // Compress GOF
  for (auto& gofInfo : sequenceInfo) {
    vmesh::VMCEncoder      encoder(params.encParams.videoAttributeCount);
    vmesh::Sequence        source;
    vmesh::Sequence        reconstruct;
    vmesh::V3cBitstream    syntax;
    vmesh::V3CParameterSet vps;  //one VPS per one GOF, initialized in compress
    syntax.setBitstreamStat(bitstreamStat);
#if CONFORMANCE_LOG_ENC
    vmesh::Logger loggerConformance;
    loggerConformance.initilalize(basePath + "_GOF_"
      + std::to_string(gofInfo.index_)+ "_conformance", true);
    encoder.setLogger(loggerConformance);
#endif
    int32_t submeshCount = params.encParams.numSubmesh;
    encoder.initializeParameterSets(
      syntax, vps, atlasCount, atlasId, gofInfo.index_, params.encParams);
    encoder.setIdIndexMapping(params.encParams);
    encoder.initializeImageSizesAndTiles(params.encParams,
                                         gofInfo.frameCount_);

    encoder.setFilePaths(sourceSubmeshPath,
                         params.inputMeshPath,
                         params.inputAttributesPaths,
                         params.reconstructedMeshPath,
                         params.reconstructedAttributesPaths,
                         params.reconstructedMaterialLibPath,
                         params.startFrame);
    encoder.setAttributeVideoCodecConfig(params.encParams);

    vmesh::tic("preprocess");
    if (params.encParams.segmentByBaseMesh) {
      printf("GOF = %d / %d \n", gofInfo.index_, sequenceInfo.gofCount());
      encoder.setKeepFilesPathPrefix(basePath + "_GOF_"
                                     + std::to_string(gofInfo.index_) + "_");
      // Load source mesh sequence
      if (!source.load(params.inputMeshPath,
                       gofInfo.startFrameIndex_,
                       gofInfo.frameCount_)) {
        std::cerr << "Error: can't load source sequence\n";
        return false;
      }
      if (!encoder.segmentByBaseMesh(gofInfo,
                                     source,
                                     sourceSubmeshPath,
                                     params.encParams)) {
        std::cerr << "Error: can't compress group of frames!\n";
        return false;
      }
    }
    vmesh::toc("preprocess");

    std::vector<std::vector<vmesh::VMCSubmesh>> encodedSubmeshes;
    encodedSubmeshes.resize(submeshCount);
    std::vector<std::vector<std::vector<int32_t>>> submeshLodMaps;
    submeshLodMaps.resize(submeshCount);
    std::vector<std::vector<std::vector<int32_t>>> submeshChildToParentMaps;
    submeshChildToParentMaps.resize(submeshCount);

    for (int32_t submeshIdx = 0; submeshIdx < params.encParams.numSubmesh;
         submeshIdx++) {
      printf("==================================================\n");
      printf("============ GOF = %d / %d Submesh %d ============\n",
             gofInfo.index_,
             sequenceInfo.gofCount(),
             submeshIdx);
      printf("==================================================\n");
      encoder.setKeepFilesPathPrefix(basePath + "_submesh_"
                                     + std::to_string(submeshIdx) + "_GOF"
                                     + std::to_string(gofInfo.index_) + "_");

      std::string sourcePath = params.inputMeshPath;
      if (params.encParams.numSubmesh > 1) {
        sourcePath = sourceSubmeshPath[submeshIdx];
      }
      // Load source mesh sequence
      if (!source.load(sourcePath,
                       gofInfo.startFrameIndex_,
                       gofInfo.frameCount_)) {
        std::cerr << "Error: can't load source sequence\n";
        return false;
      }

      if (params.encParams.analyzeGof && params.encParams.numSubmesh > 1
          && !params.encParams.segmentByBaseMesh) {
        //update gofInfo
        encoder.updateGofInfo(source, gofInfo);
      }
#if 1
      printf(
        "Frame reference structure from sequence.generate() submesh [ %d ]\n",
        submeshIdx);
      for (int fi = 0; fi < gofInfo.frameCount_; fi++) {
        printf("frame[%d]\tref: %d\tprev: %d\n",
               fi,
               gofInfo.frameInfo(fi).referenceFrameIndex,
               gofInfo.frameInfo(fi).previousFrameIndex);
      }
#endif
      //preprocessing
      vmesh::tic("preprocess");
      if (params.encParams.segmentByBaseMesh) {
        if (!encoder.preprocessSubmesh(gofInfo,
                                       encodedSubmeshes[submeshIdx],
                                       submeshIdx,
                                       params.encParams)) {
          std::cout << "Error: preprocessing failed\n";
          return false;
        }
      } else {
        if (!encoder.preprocessMeshes(gofInfo,
                                      source,
                                      encodedSubmeshes[submeshIdx],
                                      syntax,
                                      vps,
                                      submeshIdx,
                                      submeshCount,
                                      params.encParams)){
          std::cout<<"Error: preprocessing failed\n";
          return false;
        }
      }
      vmesh::toc("preprocess");

      // Compress meshes
      vmesh::tic("compress");
      if (!encoder.compressMesh(gofInfo,
                            //source,
                            encodedSubmeshes[submeshIdx],
                            syntax,
                            vps,
                            submeshIdx,
                            submeshCount,
                            params.encParams,
                            submeshLodMaps[submeshIdx],
                            submeshChildToParentMaps[submeshIdx])) {
        std::cerr << "Error: can't compress group of frames!\n";
        return false;
      }
      auto end = std::chrono::steady_clock::now();
      vmesh::toc("compress");
    }  //submeshIdx
#if CONFORMANCE_LOG_ENC
      //storing the reconstructed basemesh for conformance
    encoder.getBasemeshLog(gofInfo.frameCount_);
#endif
    encoder.updateMesh(encodedSubmeshes, syntax, params.encParams);
    encoder.compressAtlas(
      encodedSubmeshes, syntax, vps, params.encParams);

#ifdef COMPRESS_VIDEO_PAC_ENABLE
    if(params.encParams.encodeDisplacementType == 2 && params.encParams.encodeTextureVideo && params.encParams.jointTextDisp){
      vmesh::tic("compressPacked");
      auto startPac = std::chrono::steady_clock::now();
      encoder.compressVideoPac(encodedSubmeshes,
                               reconstruct,
                               submeshCount,
                               gofInfo.frameCount_,
                               frameOffset,
                               syntax,
                               vps,
                               params.encParams);

      auto endPac = std::chrono::steady_clock::now();
      double cTime =
        std::chrono::duration<double>(endPac - startPac).count() * 1000.0;
      vmesh::toc("compressPacked");
    }
#endif

    if (params.encParams.encodeDisplacementType != 0 && !params.encParams.jointTextDisp) {
      //encode geometry video sub-bitstream
      vmesh::tic("compressGeometry");
      if (params.encParams.encodeDisplacementType == 2) {
        auto startGeo = std::chrono::steady_clock::now();
        if (!encoder.compressVideoGeo(encodedSubmeshes,
                                      gofInfo.frameCount_,
                                      syntax,
                                      vps,
                                      params.encParams)) {
          std::cerr << "Error: can't compress group of frames!\n";
          return false;
        }
        auto   endGeo = std::chrono::steady_clock::now();
        double cTime =
          std::chrono::duration<double>(endGeo - startGeo).count() * 1000.0;
        //totalGeoCompressionTime +=cTime;
      } else if (params.encParams.encodeDisplacementType == 1) {
        auto startGeo = std::chrono::steady_clock::now();
        if (!encoder.compressAcGeo(encodedSubmeshes,
                                   gofInfo.frameCount_,
                                   syntax,
                                   vps,
                                   params.encParams)) {
          std::cerr << "Error: can't compress group of frames!\n";
          return false;
        }
        auto   endGeo = std::chrono::steady_clock::now();
        double cTime =
          std::chrono::duration<double>(endGeo - startGeo).count() * 1000.0;
        //totalGeoCompressionTime +=cTime;
      }
      vmesh::toc("compressGeometry");
    }
    //zippering
    if (params.encParams.zipperingMethod_ == 7) {
        // create the boundary vertex list
        encoder.zippering(gofInfo.frameCount_,
            params.encParams.numSubmesh,
            params.encParams,
            submeshLodMaps,
            params.inputMeshPath,
            gofInfo.startFrameIndex_);
    }
    // LoD-based extraction information SEI
    if(params.encParams.lodPatchesEnable && params.encParams.LoDExtractionEnable
      && (params.encParams.encodeDisplacementType == 2) && (params.encParams.subdivisionIterationCount != 0))
      encoder.addLoDExtractioninformationSEI(gofInfo.frameCount_, syntax, params.encParams);
    // component codec mapping SEI
    if((vps.getProfileTierLevel().getPtlVideoCodecGroupIdc() == vmesh::Video_MP4RA) ||
       (vps.getProfileTierLevel().getPtlNonVideoCodecGroupIdc() == vmesh::NonVideo_MP4RA)) 
      encoder.createCodecComponentMappingSei(syntax, vps, params.encParams);
    //encode attribute video sub-bitstream
    if (params.encParams.encodeTextureVideo && !params.encParams.jointTextDisp && params.encParams.numTextures > 1) {
      vmesh::tic("compressAttribute");
      int numOutputTextures = params.encParams.dracoMeshLossless ? params.encParams.numTextures : 1;
      // allocating output structures
      reconstruct.resize(gofInfo.frameCount_, numOutputTextures);
      auto startAtt = std::chrono::steady_clock::now();
      //note: when numTextures > 1, no other attributes are used.
      for (int attrIdx = 0; attrIdx < numOutputTextures; attrIdx++) {
         if (!encoder.compressVideoTexture(encodedSubmeshes,
                                    reconstruct,
                                    submeshCount,
                                    gofInfo.frameCount_,
                                    frameOffset,
                                    attrIdx,
                                    syntax,
                                    vps,
                                    params.encParams)) {
           std::cerr << "Error: can't compress group of frames!\n";
           return false;
         }
      }
      auto   endAtt = std::chrono::steady_clock::now();
      double cTime =
        std::chrono::duration<double>(endAtt - startAtt).count() * 1000.0;
      //    totalAttCompressionTime +=cTime;
      vmesh::toc("compressAttribute");
    }else if(params.encParams.videoAttributeCount > 0 && !params.encParams.jointTextDisp){
      reconstruct.attributes().resize(params.encParams.videoAttributeCount);
      vmesh::tic("compressAttribute");
      double totalAttCompressionTime = 0;
      for(int attrIdx=0; attrIdx<params.encParams.videoAttributeCount; attrIdx++){
        auto startAtt = std::chrono::steady_clock::now();
        if (!encoder.compressVideoAttribute(encodedSubmeshes,
                                      reconstruct,
                                      submeshCount,
                                      gofInfo.frameCount_,
                                      frameOffset,
                                      attrIdx,
                                      syntax,
                                      vps,
                                      params.encParams)) {
          std::cerr << "Error: can't compress group of frames!\n";
          return false;
        }
        auto   endAtt = std::chrono::steady_clock::now();
        double cTime =
          std::chrono::duration<double>(endAtt - startAtt).count() * 1000.0;
         totalAttCompressionTime +=cTime;
      }
      vmesh::toc("compressAttribute");
    }

    // Attribute extraction information SEI
    if(params.encParams.attributeExtractionEnable)
      encoder.addAttributeExtractionInformationSEI(gofInfo.frameCount_, syntax, params.encParams);

    if (params.encParams.zipperingMethod_ == 7) {
        encoder.updateReconMesh(gofInfo.frameCount_, params.encParams.numSubmesh);
    }

    if (params.encParams.numSubmesh > 1 && params.encParams.encodeTextureVideo
        && syntax.getAtlasDataStream()
             .getAtlasSequenceParameterSet(0)
             .getAsveExtension()
             .getAsveAttributeSubtextureEnabledFlag()[0] && !params.encParams.dracoMeshLossless)
      encoder.adjustTextureCoordinates(
        gofInfo.frameCount_, params.encParams.numSubmesh, params.encParams);

#if CONFORMANCE_LOG_ENC
    encoder.getAtlasLog(gofInfo.frameCount_, syntax, vps, params.encParams);
    encoder.getTileLog(gofInfo.frameCount_, syntax, vps, params.encParams);
    encoder.getMFrameLog(gofInfo.frameCount_,
        params.encParams.numSubmesh,
        params.encParams.bitDepthTexCoord);
#endif
    //zippering
    if (params.encParams.zipperingMethod_ >= 0) {
        // create the boundary vertex list
        encoder.optimizeZipperingThreshold(params.encParams, submeshLodMaps, submeshChildToParentMaps);
        encoder.zippering(gofInfo.frameCount_,
            params.encParams.numSubmesh,
            params.encParams,
            submeshLodMaps,
            params.inputMeshPath,
            gofInfo.startFrameIndex_);
        encoder.addZipperingSEI(gofInfo.frameCount_, syntax, params.encParams);
    }
    if (params.encParams.tileSubmeshMappingSEIFlag_)
        encoder.addTileSubmeshMappingSEI(gofInfo.frameCount_, syntax, params.encParams);
    if (params.encParams.attributeTransformParamsV3CSEIEnable) {
        encoder.addAttributeTransformationParamsV3cSEI(syntax);
    }

    if (params.encParams.attributeTransformParamsBaseMeshSEIEnable) {
        encoder.addAttributeTransformationParamsBaseMeshSEI(syntax);
    }
    //reconstruction : encoder keeps rec[submesh][frame]
    const uint32_t bitDepthTexCoord = params.encParams.bitDepthTexCoord;
    uint32_t faceCount =
      encoder.writeReconstructedMeshes(gofInfo.frameCount_,
                                       params.encParams.numSubmesh,
                                       bitDepthTexCoord,
                                       frameOffset,
                                       params.encParams);

    frameOffset += gofInfo.frameCount_;
    // set the correct profile and level
    encoder.setProfileAndLevels(syntax, vps, params.encParams);
    // Create V3C sample stream units
    std::cout<<"encode starts\n";
    vmesh::V3CWriter writer;
#if defined(BITSTREAM_TRACE)
    writer.setLogger(loggerHls);
#endif
    vmesh::tic("writer");
    writer.encode(syntax, vps, ssvu);
    vmesh::toc("writer");
#if CONFORMANCE_LOG_ENC
    encoder.getHighLevelSyntaxLog(syntax);
#endif

    if (params.checksum) {
      auto& gofChecksumMesh = encoder.getEncChecksum().checksumMesh();
      for (auto c : gofChecksumMesh) checksum.checksumMesh().push_back(c);

      if(params.encParams.numTextures>1){
        checksum.checksumAtts().resize( encoder.getEncChecksum().checksumAtts().size() );
        for(size_t attIdx=0; attIdx<encoder.getEncChecksum().checksumAtts().size(); attIdx++){
          auto& gofChecksumTex  = encoder.getEncChecksum().checksumAtt(attIdx);
          for (auto c : gofChecksumTex) checksum.checksumAtt(attIdx).push_back(c);
        }
      }else{
        for(size_t attIdx=0; attIdx<params.encParams.videoAttributeCount; attIdx++){
          auto& gofChecksumTex  = encoder.getEncChecksum().checksumAtt(attIdx);
          for (auto c : gofChecksumTex) checksum.checksumAtt(attIdx).push_back(c);
        }
      }
    }
    if (computeMetrics) {
      metrics.compute(source, reconstruct, params.metParams);
    }
    //    for (auto& rec : reconstruct.meshes())
    //      bitstreamStat.getFaceCount() += rec.triangleCount();
    bitstreamStat.getFaceCount() = faceCount;
  }  //sequenceInfo

  // Write V3C bistream
  vmesh::V3CWriter writer;
  vmesh::Bitstream bitstream;
#if defined(BITSTREAM_TRACE)
  vmesh::Logger loggerV3c;
  loggerV3c.initilalize(basePath + "_v3c", true);
  bitstream.setLogger(loggerV3c);
  bitstream.setTrace(true);
  writer.setLogger(loggerV3c);
#endif
  size_t headerSize =
    writer.write(ssvu, bitstream, params.encParams.forceSsvhUnitSizePrecision);
  bitstreamStat.incrHeader(headerSize);
  if (!bitstream.save(params.compressedStreamPath)) {
    std::cerr << "Error: can't save compressed bitstream!\n";
    return false;
  }
#if CONFORMANCE_LOG_ENC
  vmesh::Logger loggerBitstream;
  loggerBitstream.initilalize(basePath + "_conformance", true);
  std::vector<uint8_t> data;
  data.resize(bitstream.size());
  for(int i=0; i< bitstream.size(); i++)
      data[i] = bitstream.vector()[i];
  TRACE_BITSTRMD5("**********VDMC BITSTREAM**********\n");
  TRACE_BITSTRMD5("BITSTRMD5 = %s", checksum.getChecksum(data).c_str());
#endif
  // Write checksums
  if (params.checksum) {
    checksum.write(basePath + ".checksum");
    checksum.print();
  }

  // Display stat: metrics, duractions, bitstreams, memory and face counts
  if (computeMetrics) {
    printf("\n------- All frames metrics -----------\n");
    metrics.display();
    printf("---------------------------------------\n");
  }
  bitstreamStat.trace();
  //writer.report(ssvu, bitstream, headerSize);
  printf("\nAll frames have been encoded. \n");
  return true;
}

//============================================================================

int
main(int argc, char* argv[]) {
  std::cout << "MPEG VMESH version " << ::vmesh::version << '\n';

  // this is mandatory to print floats with full precision
  std::cout.precision(std::numeric_limits<float>::max_digits10);

  Parameters params;
  commandLineArguments.assign(argv, argv + argc);
  if (!parseParameters(argc, argv, params)) { return 1; }

  if (params.verbose) { vmesh::vout.rdbuf(std::cout.rdbuf()); }

  if (!compress(params)) {
    std::cerr << "Error: can't compress mesh sequence!\n";
    return 1;
  }

  return 0;
}
