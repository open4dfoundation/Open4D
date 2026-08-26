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
#if defined(USE_VTM_VIDEO_CODEC)
#  include "util/image.hpp"

#  include <list>
#  include <ostream>
#  include "EncoderLib/EncLib.h"
#  include "Utilities/VideoIOYuv.h"
#  include "vtmVideoEncoderCfg.hpp"
#  include <list>
#  include <fstream>
#  include <stdlib.h>
#  include <stdio.h>
#  include <fcntl.h>
#  include <assert.h>
#  include <iomanip>
#  include "EncoderLib/EncTemporalFilter.h"
#  if EXTENSION_360_VIDEO
#    include "AppEncHelper360/TExt360AppEncTop.h"
#  endif
#  include "Utilities/program_options_lite.h"

using namespace std;

namespace vmesh {

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// encoder application class
template<class T>
class vtmVideoEncoderImpl
  : public vtmVideoEncoderCfg
  , public AUWriterIf {
public:
  vtmVideoEncoderImpl(ostream& bitStream, EncLibCommon* encLibCommon);

  ~vtmVideoEncoderImpl();

  bool               encode(FrameSequence<T>&     videoSrc,
                            std::string           arguments,
                            std::vector<uint8_t>& bitstream,
                            FrameSequence<T>&     videoRec);
  bool               encodePrep(bool&             eos,
                                FrameSequence<T>& videoSrc,
                                std::string       arguments,
                                FrameSequence<T>& videoRec);
  void               createLib(const int layerIdx);
  void               destroyLib();
  int                getMaxLayers() const { return m_maxLayers; }
  void               init(AUWriterIf* auWriterIf);
  VPS*               getVPS() { return m_cEncLib.getVPS(); }
  const ChromaFormat getChromaFormatIDC() const {
    return m_cEncLib.getChromaFormatIdc();
  }
  int getBitDepth() const { return m_cEncLib.getBitDepth(ChannelType::LUMA); }

  bool getALFEnabled() { return m_cEncLib.getUseALF(); }
  int  getMaxNumALFAPS() { return m_cEncLib.getMaxNumALFAPS(); }
  int  getALFAPSIDShift() { return m_cEncLib.getALFAPSIDShift(); }
  void forceMaxNumALFAPS(int n) { m_cEncLib.setMaxNumALFAPS(n); }
  void forceALFAPSIDShift(int n) { m_cEncLib.setALFAPSIDShift(n); }

private:
  void xInitLibCfg(int layerIdx);  ///< initialize internal variables
  void xInitLib();                 ///< initialize encoder class
  void xDestroyLib();              ///< destroy encoder class
  void xWriteOutput(std::ostream&           bitstreamFile,
                    int                     iNumEncoded,
                    std::list<PelUnitBuf*>& recBufList,
                    FrameSequence<T>&       videoRec);
  void xWritePicture(const PelUnitBuf* pic, FrameSequence<T>& video);
  void xReadPicture(PelUnitBuf* pic, FrameSequence<T>& video, int frameIndex);
  void printChromaFormat();
  void printRateSummary();
  void rateStatsAccum(const AccessUnit&            au,
                      const std::vector<uint32_t>& stats);
  void outputAU(const AccessUnit& au);

  // class interface
  EncLib                 m_cEncLib;  ///< encoder class
  std::list<PelUnitBuf*> m_recBufList;
  int                    m_iFrameRcvd;
  uint32_t               m_essentialBytes;
  uint32_t               m_totalBytes;
  int                    m_outputWidth;
  int                    m_outputHeight;
  int                    m_numEncoded;
  PelStorage*            m_trueOrgPic;
  PelStorage*            m_orgPic;
  PelStorage*            m_filteredOrgPic;
  PelStorage*            m_trueOrgPicBeforeScale;
  PelStorage*            m_orgPicBeforeScale;
  PelStorage*            m_rprPic[2];
  EncTemporalFilter      m_temporalFilter;
  PelStorage*            m_filteredOrgPicForFG;
  EncTemporalFilter      m_temporalFilterForFG;
  bool                   m_flush;
  ostream&               m_bitstream;
};
}  // namespace vmesh

#endif
