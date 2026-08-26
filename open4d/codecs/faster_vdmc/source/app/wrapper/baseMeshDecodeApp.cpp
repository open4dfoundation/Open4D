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
#include "decoder.hpp"
#include "sequenceInfo.hpp"
#include "baseMeshReader.hpp"
#include "baseMeshDecoder.hpp"

struct Parameters {
  std::string                 compressedStreamPath   = {};
  std::string                 decodedMeshPath        = {};
  int32_t                     startFrame             = 536;
  int32_t                     frameCount             = 3;
  double                      framerate              = 30.;
  bool                        verbose                = false;
  bool                        checksum               = true;
};

int
main(int argc, char* argv[]) {
  std::cout << "ISO/IEC 23090-29 Annex H test decoder application" << '\n';

  auto compressedFilename = "/Users/kondrad/Code/data/vdmc/v12/soldier.bmesh";
  auto logFilename        = "/Users/kondrad/Code/data/vdmc/v12/soldier_hls_dec";

  vmesh::Bitstream                    bitstream;
  vmesh::SampleStreamV3CUnit          ssvu;
  basemesh::BaseMeshReader               baseMeshReader;
  basemesh::BaseMeshBitstream            baseMeshStream;
  basemesh::BaseMeshBitstreamGofStat     bitstreamStat;

  vmesh::Logger loggerHls;
  loggerHls.initilalize(logFilename);
#if defined(BITSTREAM_TRACE)
  bitstream.setLogger(loggerHls);
  baseMeshReader.setLogger(loggerHls);
  bitstream.setTrace(true);
#endif

  if (!bitstream.load(compressedFilename)) {
    std::cerr << "Error: can't load compressed bitstream ! ("
              << compressedFilename << ")\n";
    return false;
  }

  baseMeshReader.decode(baseMeshStream, bitstream, bitstreamStat);

  basemesh::BaseMeshDecoder basemeshDecoder;
  basemeshDecoder.setKeepFilesPathPrefix("/Users/kondrad/Code/data/vdmc/v12/");
  basemeshDecoder.setExternalBitdepth(12); // TODO check this value
  basemeshDecoder.decodeBasemeshSubbitstream(
    baseMeshStream, true, false);
  //auto meshFrameCount  = basemeshDecoder.getMeshFrameCount();
  //auto maxSubmeshCount = (int32_t)basemeshDecoder.getMaxSubmeshCount(baseMeshStream);
  std::vector<std::vector<vmesh::VMCBasemesh>>& decodedMeshes =
    basemeshDecoder.getDecodedMeshFrames();

  return 0;
}
