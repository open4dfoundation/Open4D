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
#if defined(USE_VTM_VIDEO_CODEC)

#  include "util/image.hpp"

#  include "vtmVideoDecoderCfg.hpp"

#  include <CommonLib/Picture.h>
#  include <DecoderLib/AnnexBread.h>
#  include <DecoderLib/NALread.h>
#  include <DecoderLib/DecLib.h>

namespace vmesh {

template<class T>
class vtmVideoDecoderImpl : public vtmVideoDecoderCfg {
public:
  vtmVideoDecoderImpl();

  ~vtmVideoDecoderImpl();
  uint32_t decode(const std::vector<uint8_t>& bitstream,
                  std::vector<size_t>&        videoBitDepth,
                  FrameSequence<T>&           video);
  int      m_targetOlsIdx          = 0;
  bool     m_tOlsIdxTidExternalSet = false;

private:
  void xCreateDecLib();
  void xDestroyDecLib();
  void setVideoSize(const SPS* sps);
  void xWriteOutput(PicList* pcListPic, uint32_t tId, FrameSequence<T>& video);
  void xFlushOutput(PicList*          pcListPic,
                    FrameSequence<T>& video,
                    const int         layerId = NOT_VALID);
  void xWritePicture(const Picture* pic, FrameSequence<T>& video);
  DecLib m_cDecLib;  ///< decoder class
  int    m_iPOCLastDisplay = -MAX_INT;
  int    m_iSkipFrame{};
  // std::array<int, MAX_NUM_CHANNEL_TYPE> m_outputBitDepth{};
  BitDepths                                  m_outputBitDepth;
  int                                        m_internalBitDepths;
  int                                        m_outputWidth;
  int                                        m_outputHeight;
  bool                                       m_bRGB2GBR;
  bool                                       m_newCLVS[MAX_NUM_LAYER_IDS];
  std::ofstream                              m_seiMessageFileStream;
  EnumArray<int, ChannelType>                m_BitDepths{};
  SEIAnnotatedRegions::AnnotatedRegionHeader m_arHeader;  ///< AR header
  std::map<uint32_t, SEIAnnotatedRegions::AnnotatedRegionObject>
                                  m_arObjects;  ///< AR object pool
  std::map<uint32_t, std::string> m_arLabels;   ///< AR label pool

  bool xIsNaluWithinTargetDecLayerIdSet(const InputNALUnit* nalu) const;
  bool xIsNaluWithinTargetOutputLayerIdSet(const InputNALUnit* nalu) const;
  bool isNewPicture(std::istream*          bitstreamFile,
                    class InputByteStream* bytestream);
  bool isNewAccessUnit(bool                   newPicture,
                       std::istream*          bitstreamFile,
                       class InputByteStream* bytestream);

  void xOutputAnnotatedRegions(PicList* pcListPic);
};

}  // namespace vmesh

#endif
