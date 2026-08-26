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

#  include "vtmVideoEncoderImpl.hpp"
#  include "EncoderLib/AnnexBwrite.h"

namespace vmesh {

/// encoder application class
template<typename T>
vtmVideoEncoderImpl<T>::vtmVideoEncoderImpl(ostream&      bitStream,
                                            EncLibCommon* encLibCommon)
  : m_cEncLib(encLibCommon), m_bitstream(bitStream) {
  m_iFrameRcvd     = 0;
  m_totalBytes     = 0;
  m_essentialBytes = 0;
#  if JVET_O0756_CALCULATE_HDRMETRICS
  m_metricTime = std::chrono::milliseconds(0);
#  endif
  m_numEncoded = 0;
  m_flush      = false;
}

template<typename T>
vtmVideoEncoderImpl<T>::~vtmVideoEncoderImpl() {}

template<typename T>
bool
vtmVideoEncoderImpl<T>::encode(FrameSequence<T>&     videoSrc,
                               std::string           arguments,
                               std::vector<uint8_t>& bitstream,
                               FrameSequence<T>&     videoRec) {
  const InputColourSpaceConversion snrCSC = (!m_snrInternalColourSpace)
                                              ? m_inputColourSpaceConvert
                                              : IPCOLOURSPACE_UNCHANGED;

  bool keepDoing = false;

  // call encoding function for one frame
  if (m_isField) {
    keepDoing =
      m_cEncLib.encode(snrCSC, m_recBufList, m_numEncoded, m_isTopFieldFirst);
  } else {
    keepDoing = m_cEncLib.encode(snrCSC, m_recBufList, m_numEncoded);
  }
#  if JVET_O0756_CALCULATE_HDRMETRICS
  m_metricTime = m_cEncLib.getMetricTime();
#  endif

  // output when the entire GOP was proccessed
  if (!keepDoing) {
    // write bistream to file if necessary
    if (m_numEncoded > 0) {
      xWriteOutput(m_bitstream, m_numEncoded, m_recBufList, videoRec);
    }
  }
  return keepDoing;
}

template<typename T>
bool
vtmVideoEncoderImpl<T>::encodePrep(bool&             eos,
                                   FrameSequence<T>& videoSrc,
                                   std::string       arguments,
                                   FrameSequence<T>& videoRec) {
  const InputColourSpaceConversion ipCSC  = m_inputColourSpaceConvert;
  const InputColourSpaceConversion snrCSC = (!m_snrInternalColourSpace)
                                              ? m_inputColourSpaceConvert
                                              : IPCOLOURSPACE_UNCHANGED;

  m_framesToBeEncoded =
    std::min(m_framesToBeEncoded, (int)videoSrc.frameCount());
  xReadPicture(m_orgPic, videoSrc, m_iFrameRcvd);
  xReadPicture(m_trueOrgPic, videoSrc, m_iFrameRcvd);
  if (m_gopBasedTemporalFilterEnabled) {
    m_temporalFilter.filter(m_orgPic, m_iFrameRcvd);
    m_filteredOrgPic->copyFrom(*m_orgPic);
  }

  // increase number of received frames
  m_iFrameRcvd++;

  eos = (m_isField && (m_iFrameRcvd == (m_framesToBeEncoded >> 1)))
        || (!m_isField && (m_iFrameRcvd == m_framesToBeEncoded));

  bool keepDoing = false;

  // call encoding function for one frame
  if (m_isField) {
    keepDoing = m_cEncLib.encodePrep(eos,
                                     m_flush ? 0 : m_orgPic,
                                     m_flush ? 0 : m_trueOrgPic,
                                     m_flush ? 0 : m_filteredOrgPic,
                                     snrCSC,
                                     m_recBufList,
                                     m_numEncoded,
                                     m_isTopFieldFirst);
  } else {
    keepDoing = m_cEncLib.encodePrep(eos,
                                     m_flush ? 0 : m_orgPic,
                                     m_flush ? 0 : m_trueOrgPic,
                                     m_flush ? 0 : m_filteredOrgPic,
                                     m_flush ? 0 : m_filteredOrgPicForFG,
                                     snrCSC,
                                     m_recBufList,
                                     m_numEncoded,
                                     m_rprPic);
  }
  return keepDoing;
}

template<typename T>
void
vtmVideoEncoderImpl<T>::outputAU(const AccessUnit& au) {
  const std::vector<uint32_t>& stats = writeAnnexBAccessUnit(m_bitstream, au);
  rateStatsAccum(au, stats);
  m_bitstream.flush();
}

template<typename T>
void
vtmVideoEncoderImpl<T>::rateStatsAccum(
  const AccessUnit&            au,
  const std::vector<uint32_t>& annexBsizes) {
  AccessUnit::const_iterator       it_au    = au.begin();
  vector<uint32_t>::const_iterator it_stats = annexBsizes.begin();

  for (; it_au != au.end(); it_au++, it_stats++) {
    switch ((*it_au)->m_nalUnitType) {
    case NAL_UNIT_CODED_SLICE_TRAIL:
    case NAL_UNIT_CODED_SLICE_STSA:
    case NAL_UNIT_CODED_SLICE_IDR_W_RADL:
    case NAL_UNIT_CODED_SLICE_IDR_N_LP:
    case NAL_UNIT_CODED_SLICE_CRA:
    case NAL_UNIT_CODED_SLICE_GDR:
    case NAL_UNIT_CODED_SLICE_RADL:
    case NAL_UNIT_CODED_SLICE_RASL:
    case NAL_UNIT_OPI:
    case NAL_UNIT_DCI:
    case NAL_UNIT_VPS:
    case NAL_UNIT_SPS:
    case NAL_UNIT_PPS:
    case NAL_UNIT_PH:
    case NAL_UNIT_PREFIX_APS:
    case NAL_UNIT_SUFFIX_APS: m_essentialBytes += *it_stats; break;
    default: break;
    }

    m_totalBytes += *it_stats;
  }
}

template<typename T>
void
vtmVideoEncoderImpl<T>::createLib(const int layerIdx) {
  const int sourceHeight = m_isField ? m_iSourceHeightOrg : m_sourceHeight;
  UnitArea  unitArea(m_chromaFormatIdc,
                    Area(0, 0, m_sourceWidth, sourceHeight));

  m_orgPic     = new PelStorage;
  m_trueOrgPic = new PelStorage;
  m_orgPic->create(unitArea);
  m_trueOrgPic->create(unitArea);
  if (m_sourceScalingRatioHor != 1.0 || m_sourceScalingRatioVer != 1.0) {
    UnitArea unitAreaPrescale(
      m_chromaFormatIdc,
      Area(0, 0, m_sourceWidthBeforeScale, m_sourceHeightBeforeScale));
    m_orgPicBeforeScale     = new PelStorage;
    m_trueOrgPicBeforeScale = new PelStorage;
    m_orgPicBeforeScale->create(unitAreaPrescale);
    m_trueOrgPicBeforeScale->create(unitAreaPrescale);
  }
  if (m_gopBasedTemporalFilterEnabled || m_bimEnabled) {
    m_filteredOrgPic = new PelStorage;
    m_filteredOrgPic->create(unitArea);
  }

  if (m_resChangeInClvsEnabled && m_gopBasedRPREnabledFlag) {
    UnitArea unitAreaRPR10(m_chromaFormatIdc,
                           Area(0, 0, m_sourceWidth, sourceHeight));
    UnitArea unitAreaRPR20(m_chromaFormatIdc,
                           Area(0, 0, m_sourceWidth / 2, sourceHeight / 2));
    m_rprPic[0] = new PelStorage;
    m_rprPic[0]->create(unitAreaRPR10);
    m_rprPic[1] = new PelStorage;
    m_rprPic[1]->create(unitAreaRPR20);
  }

  if (m_fgcSEIAnalysisEnabled && m_fgcSEIExternalDenoised.empty()) {
    m_filteredOrgPicForFG = new PelStorage;
    m_filteredOrgPicForFG->create(unitArea);
  }
  if (m_bimEnabled) {
    std::map<int, int*> adaptQPmap;
    m_cEncLib.setAdaptQPmap(adaptQPmap);
  }

  // initialize internal class & member variables and VPS
  xInitLibCfg(layerIdx);
  const int layerId = m_cEncLib.getVPS() == nullptr
                        ? 0
                        : m_cEncLib.getVPS()->getLayerId(layerIdx);

  // create the encoder
  m_cEncLib.create(layerId);

  // create the output buffer
  for (int i = 0; i < (m_gopSize + 1 + (m_isField ? 1 : 0)); i++) {
    m_recBufList.push_back(new PelUnitBuf);
  }
  xInitLib();
#  if PCC_ME_EXT
  if (m_usePCCExt) {
    // Note JR: must be given from the function parameters
    printf("\nReading the aux info files\n");
    FILE* patchFile = NULL;
    patchFile       = fopen(m_patchInfoFileName.c_str(), "rb");
    for (int i = 0; i < PCC_ME_EXT_MAX_NUM_FRAMES; i++) {
      size_t readSize =
        fread(&g_vtmnumPatches[i], sizeof(long long), 1, patchFile);
      if (readSize != 1 && readSize != 0) {
        printf("error: Wrong Patch data group file");
      }
      for (int patchIdx = 0; patchIdx < g_vtmnumPatches[i]; patchIdx++) {
        readSize = fread(
          &g_vtmprojectionIndex[i][patchIdx], sizeof(long long), 1, patchFile);
        if (readSize != 1) { printf("error: Wrong Auxiliary data format"); }
        readSize = fread(
          g_vtmpatch2DInfo[i][patchIdx], sizeof(long long), 4, patchFile);
        if (readSize != 4) { printf("error: Wrong Auxiliary data format"); }
        readSize = fread(
          g_vtmpatch3DInfo[i][patchIdx], sizeof(long long), 3, patchFile);
        if (readSize != 3) { printf("error: Wrong Auxiliary data format"); }
      }
    }
    fclose(patchFile);
  }
#  endif
  printChromaFormat();

#  if EXTENSION_360_VIDEO
  m_ext360 = new TExt360AppEncTop(*this,
                                  m_cEncLib.getGOPEncoder()->getExt360Data(),
                                  *(m_cEncLib.getGOPEncoder()),
                                  *m_orgPic);
#  endif

  if (m_gopBasedTemporalFilterEnabled || m_bimEnabled) {
    m_temporalFilter.init(m_frameSkip,
                          m_inputBitDepth,
                          m_msbExtendedBitDepth,
                          m_internalBitDepth,
                          m_sourceWidth,
                          sourceHeight,
                          m_sourcePadding,
                          m_clipInputVideoToRec709Range,
                          m_inputFileName,
                          m_chromaFormatIdc,
                          m_inputColourSpaceConvert,
                          m_iQP,
                          m_gopBasedTemporalFilterStrengths,
                          m_gopBasedTemporalFilterPastRefs,
                          m_gopBasedTemporalFilterFutureRefs,
                          m_firstValidFrame,
                          m_lastValidFrame,
                          m_gopBasedTemporalFilterEnabled,
                          m_cEncLib.getAdaptQPmap(),
                          m_cEncLib.getBIM(),
                          m_ctuSize);
  }
  if (m_fgcSEIAnalysisEnabled && m_fgcSEIExternalDenoised.empty()) {
    m_temporalFilterForFG.init(m_frameSkip,
                               m_inputBitDepth,
                               m_msbExtendedBitDepth,
                               m_internalBitDepth,
                               m_sourceWidth,
                               sourceHeight,
                               m_sourcePadding,
                               m_clipInputVideoToRec709Range,
                               m_inputFileName,
                               m_chromaFormatIdc,
                               m_inputColourSpaceConvert,
                               m_iQP,
                               m_fgcSEITemporalFilterStrengths,
                               m_fgcSEITemporalFilterPastRefs,
                               m_fgcSEITemporalFilterFutureRefs,
                               m_firstValidFrame,
                               m_lastValidFrame,
                               true,
                               m_cEncLib.getAdaptQPmap(),
                               m_cEncLib.getBIM(),
                               m_ctuSize);
  }
}

template<typename T>
void
vtmVideoEncoderImpl<T>::xInitLibCfg(int layerIdx) {
  VPS& vps = *m_cEncLib.getVPS();
  if (m_targetOlsIdx != 500) {
    vps.m_targetOlsIdx = m_targetOlsIdx;
  } else {
    vps.m_targetOlsIdx = -1;
  }

  vps.setMaxLayers(m_maxLayers);

  if (vps.getMaxLayers() > 1) {
    vps.setVPSId(
      1);  // JVET_P0205 vps_video_parameter_set_id shall be greater than 0 for multi-layer coding
  } else {
    vps.setVPSId(0);
    vps.setEachLayerIsAnOlsFlag(1);  // If vps_max_layers_minus1 is equal to 0,
      // the value of vps_each_layer_is_an_ols_flag is inferred to be equal to 1.
      // Otherwise, when vps_all_independent_layers_flag is equal to 0,
      // the value of vps_each_layer_is_an_ols_flag is inferred to be equal to 0.
  }
  vps.setMaxSubLayers(m_maxSublayers);
  if (vps.getMaxLayers() > 1 && vps.getMaxSubLayers() > 1) {
    vps.setDefaultPtlDpbHrdMaxTidFlag(m_defaultPtlDpbHrdMaxTidFlag);
  }
  if (vps.getMaxLayers() > 1) {
    vps.setAllIndependentLayersFlag(m_allIndependentLayersFlag);
    if (!vps.getAllIndependentLayersFlag()) {
      vps.setEachLayerIsAnOlsFlag(0);
      for (int i = 0; i < m_maxTempLayer; i++) { vps.setPredDirection(i, 0); }
      for (int i = 0; i < m_predDirectionArray.size(); i++) {
        if (m_predDirectionArray[i] != ' ') {
          vps.setPredDirection(i >> 1, int(m_predDirectionArray[i] - 48));
        }
      }
    }
  }

  m_cfgVPSParameters.m_maxTidILRefPicsPlus1.resize(
    vps.getMaxLayers(), std::vector<uint32_t>(vps.getMaxLayers(), MAX_TLAYER));
  for (int i = 0; i < vps.getMaxLayers(); i++) {
    vps.setGeneralLayerIdx(m_layerId[i], i);
    vps.setLayerId(i, m_layerId[i]);

    if (i > 0 && !vps.getAllIndependentLayersFlag()) {
      vps.setIndependentLayerFlag(i, m_numRefLayers[i] ? false : true);

      if (!vps.getIndependentLayerFlag(i)) {
        for (int j = 0, k = 0; j < i; j++) {
          if (m_refLayerIdxStr[i].find(to_string(j)) != std::string::npos) {
            vps.setDirectRefLayerFlag(i, j, true);
            vps.setInterLayerRefIdc(i, j, k);
            vps.setDirectRefLayerIdx(i, k++, j);
          } else {
            vps.setDirectRefLayerFlag(i, j, false);
          }
        }
        string::size_type beginStr =
          m_maxTidILRefPicsPlus1Str[i].find_first_not_of(" ", 0);
        string::size_type endStr =
          m_maxTidILRefPicsPlus1Str[i].find_first_of(" ", beginStr);
        int t = 0;
        while (string::npos != beginStr || string::npos != endStr) {
          m_cfgVPSParameters.m_maxTidILRefPicsPlus1[i][t++] = std::stoi(
            m_maxTidILRefPicsPlus1Str[i].substr(beginStr, endStr - beginStr));
          beginStr =
            m_maxTidILRefPicsPlus1Str[i].find_first_not_of(" ", endStr);
          endStr = m_maxTidILRefPicsPlus1Str[i].find_first_of(" ", beginStr);
        }
      }
    }
  }

  if (vps.getMaxLayers() > 1) {
    if (vps.getAllIndependentLayersFlag()) {
      vps.setEachLayerIsAnOlsFlag(m_eachLayerIsAnOlsFlag);
      if (vps.getEachLayerIsAnOlsFlag() == 0) {
        vps.setOlsModeIdc(
          2);  // When vps_all_independent_layers_flag is equal to 1 and vps_each_layer_is_an_ols_flag
        // is equal to 0, the value of vps_ols_mode_idc is inferred to be equal to 2
      }
    }
    if (!vps.getEachLayerIsAnOlsFlag()) {
      if (!vps.getAllIndependentLayersFlag()) {
        vps.setOlsModeIdc(m_olsModeIdc);
      }
      if (vps.getOlsModeIdc() == 2) {
        vps.setNumOutputLayerSets(m_numOutputLayerSets);
        for (int i = 1; i < vps.getNumOutputLayerSets(); i++) {
          for (int j = 0; j < vps.getMaxLayers(); j++) {
            if (m_olsOutputLayerStr[i].find(to_string(j))
                != std::string::npos) {
              vps.setOlsOutputLayerFlag(i, j, 1);
            } else {
              vps.setOlsOutputLayerFlag(i, j, 0);
            }
          }
        }
      }
    }
  }
  CHECK(m_numPtlsInVps == 0,
        "There has to be at least one PTL structure in the VPS.");
  vps.setNumPtls(m_numPtlsInVps);
  vps.setPtPresentFlag(0, true);
  for (int i = 0; i < vps.getNumPtls(); i++) {
    if (i > 0) vps.setPtPresentFlag(i, m_ptPresentInPtl[i] != 0);
    vps.setPtlMaxTemporalId(i, vps.getMaxSubLayers() - 1);
  }
  for (int i = 0; i < vps.getNumOutputLayerSets(); i++) {
    vps.setOlsPtlIdx(i, m_olsPtlIdx[i]);
  }
  ProfileTierLevel ptl;
  ptl.setLevelIdc(m_level);
  ptl.setProfileIdc(m_profile);
  ptl.setTierFlag(m_levelTier);
  ptl.setFrameOnlyConstraintFlag(m_frameOnlyConstraintFlag);
  CHECK(m_numRefLayers[layerIdx] > 0 && !m_multiLayerEnabledFlag,
        "ptl_multilayer_enabled_flag shall be equal to 1 when target layer "
        "use inter layer prediction");
  ptl.setMultiLayerEnabledFlag(m_multiLayerEnabledFlag);
  CHECK((m_profile == Profile::MAIN_10 || m_profile == Profile::MAIN_10_444
         || m_profile == Profile::MAIN_10_STILL_PICTURE
         || m_profile == Profile::MAIN_10_444_STILL_PICTURE
         || m_profile == Profile::MAIN_12
         || m_profile == Profile::MAIN_12_INTRA
         || m_profile == Profile::MAIN_12_STILL_PICTURE
         || m_profile == Profile::MAIN_12_444
         || m_profile == Profile::MAIN_12_444_INTRA
         || m_profile == Profile::MAIN_12_444_STILL_PICTURE
         || m_profile == Profile::MAIN_16_444
         || m_profile == Profile::MAIN_16_444_INTRA
         || m_profile == Profile::MAIN_16_444_STILL_PICTURE)
          && m_multiLayerEnabledFlag,
        "ptl_multilayer_enabled_flag shall be equal to 0 for non-multilayer "
        "profiles");
  ptl.setNumSubProfile(m_numSubProfile);
  for (int i = 0; i < m_numSubProfile; i++) {
    ptl.setSubProfileIdc(i, m_subProfile[i]);
  }
  if (0 < layerIdx) { ptl.setLevelIdc(m_levelPtl[layerIdx]); }
  CHECK(layerIdx >= vps.getNumPtls(),
        "Insufficient number of Profile/Tier/Level entries in VPS. Consider "
        "increasing NumPTLsInVPS");
  vps.setProfileTierLevel(layerIdx, ptl);

  vps.setVPSExtensionFlag(false);
#  if PCC_ME_EXT
  m_cEncLib.setUsePCCExt(m_usePCCExt);
  if (m_usePCCExt) {
    m_cEncLib.setBlockToPatchFileName(m_blockToPatchFileName);
    m_cEncLib.setOccupancyMapFileName(m_occupancyMapFileName);
  }
#  endif
  m_cEncLib.setProfile(m_profile);
  m_cEncLib.setTierLevel(m_levelTier, m_level);
  m_cEncLib.setFrameOnlyConstraintFlag(m_frameOnlyConstraintFlag);
  m_cEncLib.setMultiLayerEnabledFlag(m_multiLayerEnabledFlag);
  m_cEncLib.setNumSubProfile(m_numSubProfile);
  for (int i = 0; i < m_numSubProfile; i++) {
    m_cEncLib.setSubProfile(i, m_subProfile[i]);
  }

  m_cEncLib.setPrintMSEBasedSequencePSNR(m_printMSEBasedSequencePSNR);
  m_cEncLib.setPrintFrameMSE(m_printFrameMSE);
  m_cEncLib.setPrintHexPsnr(m_printHexPsnr);
  m_cEncLib.setPrintSequenceMSE(m_printSequenceMSE);
  m_cEncLib.setPrintMSSSIM(m_printMSSSIM);
  m_cEncLib.setPrintWPSNR(m_printWPSNR);
  m_cEncLib.setPrintHightPrecEncTime(m_printHighPrecEncTime);
  m_cEncLib.setCabacZeroWordPaddingEnabled(m_cabacZeroWordPaddingEnabled);

  m_cEncLib.setFrameRate(m_frameRate);
  m_cEncLib.setFrameSkip(m_frameSkip);
  m_cEncLib.setTemporalSubsampleRatio(m_temporalSubsampleRatio);
  m_cEncLib.setSourceWidth(m_sourceWidth);
  m_cEncLib.setSourceHeight(m_sourceHeight);
  m_cEncLib.setConformanceWindow(
    m_confWinLeft / SPS::getWinUnitX(m_inputChromaFormatIDC),
    m_confWinRight / SPS::getWinUnitX(m_inputChromaFormatIDC),
    m_confWinTop / SPS::getWinUnitY(m_inputChromaFormatIDC),
    m_confWinBottom / SPS::getWinUnitY(m_inputChromaFormatIDC));
  m_cEncLib.setScalingRatio(m_scalingRatioHor, m_scalingRatioVer);
  m_cEncLib.setGOPBasedRPREnabledFlag(m_gopBasedRPREnabledFlag);
  m_cEncLib.setGOPBasedRPRQPThreshold(m_gopBasedRPRQPThreshold);
  m_cEncLib.setScalingRatio2(m_scalingRatioHor2, m_scalingRatioVer2);
  m_cEncLib.setScalingRatio3(m_scalingRatioHor3, m_scalingRatioVer3);
  m_cEncLib.setPsnrThresholdRPR(
    m_psnrThresholdRPR, m_psnrThresholdRPR2, m_psnrThresholdRPR3);
  m_cEncLib.setQpOffsetRPR(m_qpOffsetRPR, m_qpOffsetRPR2, m_qpOffsetRPR3);
  m_cEncLib.setQpOffsetChromaRPR(
    m_qpOffsetChromaRPR, m_qpOffsetChromaRPR2, m_qpOffsetChromaRPR3);
  m_cEncLib.setRprFunctionalityTestingEnabledFlag(
    m_rprFunctionalityTestingEnabledFlag);
  m_cEncLib.setRprSwitchingSegmentSize(m_rprSwitchingSegmentSize);
  m_cEncLib.setRprPopulatePPSatIntraFlag(m_rprPopulatePPSatIntraFlag);
  m_cEncLib.setRprEnabled(m_rprEnabledFlag);
  m_cEncLib.setResChangeInClvsEnabled(m_resChangeInClvsEnabled);
  m_cEncLib.setSwitchPocPeriod(m_switchPocPeriod);
  m_cEncLib.setUpscaledOutput(m_upscaledOutput);
  m_cEncLib.setUpscaleFilerForDisplay(m_upscaleFilterForDisplay);
  m_cEncLib.setFramesToBeEncoded(m_framesToBeEncoded);
  m_cEncLib.setValidFrames(m_firstValidFrame, m_lastValidFrame);
  m_cEncLib.setAvoidIntraInDepLayer(m_avoidIntraInDepLayer);

  m_cEncLib.setRefLayerMetricsEnabled(m_refMetricsEnabled);
  m_cEncLib.setRefLayerRescaledAvailable(false);

  //====== SPS constraint flags =======
  m_cEncLib.setGciPresentFlag(m_gciPresentFlag);
  if (m_cEncLib.getGciPresentFlag()) {
    m_cEncLib.setNonPackedConstraintFlag(m_nonPackedConstraintFlag);
    m_cEncLib.setNonProjectedConstraintFlag(m_nonProjectedConstraintFlag);
    m_cEncLib.setOneTilePerPicConstraintFlag(m_oneTilePerPicConstraintFlag);
    m_cEncLib.setPicHeaderInSliceHeaderConstraintFlag(
      m_picHeaderInSliceHeaderConstraintFlag);
    m_cEncLib.setOneSlicePerPicConstraintFlag(m_oneSlicePerPicConstraintFlag);
    m_cEncLib.setNoIdrRplConstraintFlag(m_noIdrRplConstraintFlag);
    CHECK(m_noIdrRplConstraintFlag && m_idrRefParamList,
          "IDR RPL shall be deactivated when gci_no_idr_rpl_constraint_flag "
          "equal to 1");

    m_cEncLib.setNoRectSliceConstraintFlag(m_noRectSliceConstraintFlag);
    CHECK(m_noRectSliceConstraintFlag && !m_rasterSliceFlag,
          "Rectangular slice shall be deactivated when "
          "gci_no_rectangular_slice_constraint_flag equal to 1");

    m_cEncLib.setOneSlicePerSubpicConstraintFlag(
      m_oneSlicePerSubpicConstraintFlag);
    CHECK(m_oneSlicePerSubpicConstraintFlag && !m_singleSlicePerSubPicFlag,
          "Each picture shall consist of one and only one rectangular slice "
          "when gci_one_slice_per_subpic_constraint_flag equal to 1");

    m_cEncLib.setNoSubpicInfoConstraintFlag(m_noSubpicInfoConstraintFlag);
    CHECK(m_noSubpicInfoConstraintFlag && m_subPicInfoPresentFlag,
          "Subpicture information shall not present when "
          "gci_no_subpic_info_constraint_flag equal to 1");
    m_cEncLib.setOnePictureOnlyConstraintFlag(m_onePictureOnlyConstraintFlag);
    m_cEncLib.setIntraOnlyConstraintFlag(m_intraOnlyConstraintFlag);
    m_cEncLib.setNoIdrConstraintFlag(m_noIdrConstraintFlag);
    m_cEncLib.setNoGdrConstraintFlag(m_noGdrConstraintFlag);
    m_cEncLib.setAllLayersIndependentConstraintFlag(
      m_allLayersIndependentConstraintFlag);
    m_cEncLib.setNoCuQpDeltaConstraintFlag(m_noCuQpDeltaConstraintFlag);

    m_cEncLib.setNoTrailConstraintFlag(m_noTrailConstraintFlag);
    CHECK(
      m_noTrailConstraintFlag && m_intraPeriod != 1,
      "TRAIL shall be deactivated when m_noTrailConstraintFlag is equal to 1");

    m_cEncLib.setNoStsaConstraintFlag(m_noStsaConstraintFlag);
    CHECK(
      m_noStsaConstraintFlag
        && (m_intraPeriod != 1 || xHasNonZeroTemporalID()),
      "STSA shall be deactivated when m_noStsaConstraintFlag is equal to 1");

    m_cEncLib.setNoRaslConstraintFlag(m_noRaslConstraintFlag);
    CHECK(
      m_noRaslConstraintFlag && (m_intraPeriod != 1 || xHasLeadingPicture()),
      "RASL shall be deactivated when m_noRaslConstraintFlag is equal to 1");

    m_cEncLib.setNoRadlConstraintFlag(m_noRadlConstraintFlag);
    CHECK(
      m_noRadlConstraintFlag && (m_intraPeriod != 1 || xHasLeadingPicture()),
      "RADL shall be deactivated when m_noRadlConstraintFlag is equal to 1");

    m_cEncLib.setNoCraConstraintFlag(m_noCraConstraintFlag);
    CHECK(m_noCraConstraintFlag && (m_intraRefreshType == 1),
          "CRA shall be deactivated when m_noCraConstraintFlag is equal to 1");

    m_cEncLib.setNoRprConstraintFlag(m_noRprConstraintFlag);
    CHECK(m_noRprConstraintFlag && m_rprEnabledFlag,
          "Reference picture resampling shall be deactivated when "
          "m_noRprConstraintFlag is equal to 1");

    m_cEncLib.setNoResChangeInClvsConstraintFlag(
      m_noResChangeInClvsConstraintFlag);
    CHECK(m_noResChangeInClvsConstraintFlag && m_resChangeInClvsEnabled,
          "Resolution change in CLVS shall be deactivated when "
          "m_noResChangeInClvsConstraintFlag is equal to 1");

    m_cEncLib.setMaxBitDepthConstraintIdc(m_maxBitDepthConstraintIdc);
    CHECK(m_internalBitDepth[ChannelType::LUMA] > m_maxBitDepthConstraintIdc,
          "Internal bit depth shall be less than or equal to "
          "m_maxBitDepthConstraintIdc");

    m_cEncLib.setMaxChromaFormatConstraintIdc(m_maxChromaFormatConstraintIdc);
    CHECK(m_chromaFormatIdc > m_maxChromaFormatConstraintIdc,
          "Chroma format Idc shall be less than or equal to "
          "m_maxBitDepthConstraintIdc");

    m_cEncLib.setNoMttConstraintFlag(m_noMttConstraintFlag);
    CHECK(
      m_noMttConstraintFlag
        && (m_uiMaxMTTHierarchyDepth || m_uiMaxMTTHierarchyDepthI
            || m_uiMaxMTTHierarchyDepthIChroma),
      "Mtt shall be deactivated when m_bNoMttConstraintFlag is equal to 1");

    m_cEncLib.setNoQtbttDualTreeIntraConstraintFlag(
      m_noQtbttDualTreeIntraConstraintFlag);
    CHECK(m_noQtbttDualTreeIntraConstraintFlag && m_dualTree,
          "Dual tree shall be deactivated when "
          "m_bNoQtbttDualTreeIntraConstraintFlag is equal to 1");

    m_cEncLib.setMaxLog2CtuSizeConstraintIdc(m_maxLog2CtuSizeConstraintIdc);
    CHECK(m_ctuSize > (1 << (m_maxLog2CtuSizeConstraintIdc)),
          "CTUSize shall be less than or equal to 1 << m_maxLog2CtuSize");

    m_cEncLib.setNoPartitionConstraintsOverrideConstraintFlag(
      m_noPartitionConstraintsOverrideConstraintFlag);
    CHECK(m_noPartitionConstraintsOverrideConstraintFlag
            && m_SplitConsOverrideEnabledFlag,
          "Partition override shall be deactivated when "
          "m_noPartitionConstraintsOverrideConstraintFlag is equal to 1");

    m_cEncLib.setNoSaoConstraintFlag(m_noSaoConstraintFlag);
    CHECK(
      m_noSaoConstraintFlag && m_useSao,
      "SAO shall be deactivated when m_bNoSaoConstraintFlag is equal to 1");

    m_cEncLib.setNoAlfConstraintFlag(m_noAlfConstraintFlag);
    CHECK(
      m_noAlfConstraintFlag && m_alf,
      "ALF shall be deactivated when m_bNoAlfConstraintFlag is equal to 1");

    m_cEncLib.setNoCCAlfConstraintFlag(m_noCCAlfConstraintFlag);
    CHECK(
      m_noCCAlfConstraintFlag && m_ccalf,
      "CCALF shall be deactivated when m_noCCAlfConstraintFlag is equal to 1");

    m_cEncLib.setNoWeightedPredictionConstraintFlag(
      m_noWeightedPredictionConstraintFlag);
    CHECK(m_noWeightedPredictionConstraintFlag
            && (m_useWeightedPred || m_useWeightedBiPred),
          "Weighted Prediction shall be deactivated when "
          "m_bNoWeightedPredictionConstraintFlag is equal to 1");

    m_cEncLib.setNoRefWraparoundConstraintFlag(
      m_noRefWraparoundConstraintFlag);
    CHECK(m_noRefWraparoundConstraintFlag && m_wrapAround,
          "Wrap around shall be deactivated when "
          "m_bNoRefWraparoundConstraintFlag is equal to 1");

    m_cEncLib.setNoTemporalMvpConstraintFlag(m_noTemporalMvpConstraintFlag);
    CHECK(m_noTemporalMvpConstraintFlag && m_TMVPModeId,
          "Temporal MVP shall be deactivated when "
          "m_bNoTemporalMvpConstraintFlag is equal to 1");

    m_cEncLib.setNoSbtmvpConstraintFlag(m_noSbtmvpConstraintFlag);
    CHECK(m_noSbtmvpConstraintFlag && m_sbTmvpEnableFlag,
          "SbTMVP shall be deactivated when m_bNoSbtmvpConstraintFlag is "
          "equal to 1");

    m_cEncLib.setNoAmvrConstraintFlag(m_noAmvrConstraintFlag);
    CHECK(
      m_noAmvrConstraintFlag && (m_ImvMode != IMV_OFF || m_AffineAmvr),
      "AMVR shall be deactivated when m_bNoAmvrConstraintFlag is equal to 1");

    m_cEncLib.setNoBdofConstraintFlag(m_noBdofConstraintFlag);
    CHECK(
      m_noBdofConstraintFlag && m_BIO,
      "BIO shall be deactivated when m_bNoBdofConstraintFlag is equal to 1");

    m_cEncLib.setNoDmvrConstraintFlag(m_noDmvrConstraintFlag);
    CHECK(
      m_noDmvrConstraintFlag && m_DMVR,
      "DMVR shall be deactivated when m_noDmvrConstraintFlag is equal to 1");

    m_cEncLib.setNoCclmConstraintFlag(m_noCclmConstraintFlag);
    CHECK(
      m_noCclmConstraintFlag && m_LMChroma,
      "CCLM shall be deactivated when m_bNoCclmConstraintFlag is equal to 1");

    m_cEncLib.setNoMtsConstraintFlag(m_noMtsConstraintFlag);
    CHECK(
      m_noMtsConstraintFlag && (m_mtsMode || m_mtsImplicitIntra),
      "MTS shall be deactivated when m_bNoMtsConstraintFlag is equal to 1");

    m_cEncLib.setNoSbtConstraintFlag(m_noSbtConstraintFlag);
    CHECK(m_noSbtConstraintFlag && m_SBT,
          "SBT shall be deactivated when "
          "mm_noSbtConstraintFlag_nonPackedConstraintFlag is equal to 1");

    m_cEncLib.setNoAffineMotionConstraintFlag(m_noAffineMotionConstraintFlag);
    CHECK(m_noAffineMotionConstraintFlag && m_Affine,
          "Affine shall be deactivated when m_bNoAffineMotionConstraintFlag "
          "is equal to 1");

    m_cEncLib.setNoBcwConstraintFlag(m_noBcwConstraintFlag);
    CHECK(
      m_noBcwConstraintFlag && m_bcw,
      "BCW shall be deactivated when m_bNoBcwConstraintFlag is equal to 1");

    m_cEncLib.setNoIbcConstraintFlag(m_noIbcConstraintFlag);
    CHECK(m_noIbcConstraintFlag && m_IBCMode,
          "IBC shall be deactivated when m_noIbcConstraintFlag is equal to 1");

    m_cEncLib.setNoCiipConstraintFlag(m_noCiipConstraintFlag);
    CHECK(
      m_noCiipConstraintFlag && m_ciip,
      "CIIP shall be deactivated when m_bNoCiipConstraintFlag is equal to 1");

    m_cEncLib.setNoGeoConstraintFlag(m_noGeoConstraintFlag);
    CHECK(m_noGeoConstraintFlag && m_Geo,
          "GEO shall be deactivated when m_noGeoConstraintFlag is equal to 1");

    m_cEncLib.setNoLadfConstraintFlag(m_noLadfConstraintFlag);
    CHECK(
      m_noLadfConstraintFlag && m_LadfEnabed,
      "LADF shall be deactivated when m_bNoLadfConstraintFlag is equal to 1");

    m_cEncLib.setNoTransformSkipConstraintFlag(
      m_noTransformSkipConstraintFlag);
    CHECK(m_noTransformSkipConstraintFlag && m_useTransformSkip,
          "Transform skip shall be deactivated when "
          "m_noTransformSkipConstraintFlag is equal to 1");

    m_cEncLib.setNoLumaTransformSize64ConstraintFlag(
      m_noLumaTransformSize64ConstraintFlag);
    CHECK(m_noLumaTransformSize64ConstraintFlag && m_log2MaxTbSize > 5,
          "Max transform size shall be less than 64 when "
          "m_noLumaTransformSize64ConstraintFlag is equal to 1");

    m_cEncLib.setNoBDPCMConstraintFlag(m_noBDPCMConstraintFlag);
    CHECK(
      m_noBDPCMConstraintFlag && m_useBDPCM,
      "BDPCM shall be deactivated when m_noBDPCMConstraintFlag is equal to 1");

    m_cEncLib.setNoJointCbCrConstraintFlag(m_noJointCbCrConstraintFlag);
    CHECK(m_noJointCbCrConstraintFlag && m_jointCbCrMode,
          "JCCR shall be deactivated when m_noJointCbCrConstraintFlag is "
          "equal to 1");

    m_cEncLib.setNoDepQuantConstraintFlag(m_noDepQuantConstraintFlag);
    CHECK(m_noDepQuantConstraintFlag && m_depQuantEnabledFlag,
          "DQ shall be deactivated when m_bNoDepQuantConstraintFlag is equal "
          "to 1");

    m_cEncLib.setNoSignDataHidingConstraintFlag(
      m_noSignDataHidingConstraintFlag);
    CHECK(m_noSignDataHidingConstraintFlag && m_signDataHidingEnabledFlag,
          "SDH shall be deactivated when m_bNoSignDataHidingConstraintFlag is "
          "equal to 1");

    m_cEncLib.setNoApsConstraintFlag(m_noApsConstraintFlag);
    CHECK(m_noApsConstraintFlag
            && (m_lmcsEnabled || (m_useScalingListId != SCALING_LIST_OFF)),
          "LMCS and explict scaling list shall be deactivated when "
          "m_noApsConstraintFlag is equal to 1");

    m_cEncLib.setNoMrlConstraintFlag(m_noMrlConstraintFlag);
    CHECK(m_noMrlConstraintFlag && m_MRL,
          "MRL shall be deactivated when m_noMrlConstraintFlag is equal to 1");

    m_cEncLib.setNoIspConstraintFlag(m_noIspConstraintFlag);
    CHECK(m_noIspConstraintFlag && m_ISP,
          "ISP shall be deactivated when m_noIspConstraintFlag is equal to 1");

    m_cEncLib.setNoMipConstraintFlag(m_noMipConstraintFlag);
    CHECK(m_noMipConstraintFlag && m_MIP,
          "MIP shall be deactivated when m_noMipConstraintFlag is equal to 1");

    m_cEncLib.setNoLfnstConstraintFlag(m_noLfnstConstraintFlag);
    CHECK(
      m_noLfnstConstraintFlag && m_LFNST,
      "LFNST shall be deactivated when m_noLfnstConstraintFlag is equal to 1");

    m_cEncLib.setNoMmvdConstraintFlag(m_noMmvdConstraintFlag);
    CHECK(
      m_noMmvdConstraintFlag && m_MMVD,
      "MMVD shall be deactivated when m_noMmvdConstraintFlag is equal to 1");

    m_cEncLib.setNoSmvdConstraintFlag(m_noSmvdConstraintFlag);
    CHECK(
      m_noSmvdConstraintFlag && m_SMVD,
      "SMVD shall be deactivated when m_noSmvdConstraintFlag is equal to 1");

    m_cEncLib.setNoProfConstraintFlag(m_noProfConstraintFlag);
    CHECK(
      m_noProfConstraintFlag && m_PROF,
      "PROF shall be deactivated when m_noProfConstraintFlag is equal to 1");

    m_cEncLib.setNoPaletteConstraintFlag(m_noPaletteConstraintFlag);
    CHECK(m_noPaletteConstraintFlag && m_PLTMode,
          "Palette shall be deactivated when m_noPaletteConstraintFlag is "
          "equal to 1");

    m_cEncLib.setNoActConstraintFlag(m_noActConstraintFlag);
    CHECK(m_noActConstraintFlag && m_useColorTrans,
          "ACT shall be deactivated when m_noActConstraintFlag is equal to 1");

    m_cEncLib.setNoLmcsConstraintFlag(m_noLmcsConstraintFlag);
    CHECK(
      m_noLmcsConstraintFlag && m_lmcsEnabled,
      "LMCS shall be deactivated when m_noLmcsConstraintFlag is equal to 1");

    m_cEncLib.setNoExplicitScaleListConstraintFlag(
      m_noExplicitScaleListConstraintFlag);
    CHECK(m_noExplicitScaleListConstraintFlag
            && m_useScalingListId != SCALING_LIST_OFF,
          "Explicit scaling list shall be deactivated when "
          "m_noExplicitScaleListConstraintFlag is equal to 1");

    m_cEncLib.setNoVirtualBoundaryConstraintFlag(
      m_noVirtualBoundaryConstraintFlag);
    CHECK(m_noVirtualBoundaryConstraintFlag && m_virtualBoundariesEnabledFlag,
          "Virtuall boundaries shall be deactivated when "
          "m_noVirtualBoundaryConstraintFlag is equal to 1");
    m_cEncLib.setNoChromaQpOffsetConstraintFlag(
      m_noChromaQpOffsetConstraintFlag);
    CHECK(m_noChromaQpOffsetConstraintFlag && m_cuChromaQpOffsetSubdiv,
          "Chroma Qp offset shall be 0 when m_noChromaQpOffsetConstraintFlag "
          "is equal to 1");
    m_cEncLib.setAllRapPicturesFlag(m_allRapPicturesFlag);
    m_cEncLib.setNoExtendedPrecisionProcessingConstraintFlag(
      m_noExtendedPrecisionProcessingConstraintFlag);
    CHECK(m_noExtendedPrecisionProcessingConstraintFlag
            && m_extendedPrecisionProcessingFlag,
          "ExtendedPrecision shall be deactivated when "
          "m_noExtendedPrecisionProcessingConstraintFlag is equal to 1");
    m_cEncLib.setNoTsResidualCodingRiceConstraintFlag(
      m_noTsResidualCodingRiceConstraintFlag);
    CHECK(m_noTsResidualCodingRiceConstraintFlag && m_tsrcRicePresentFlag,
          "TSRCRicePresent shall be deactivated when "
          "m_noTsResidualCodingRiceConstraintFlag is equal to 1");
    m_cEncLib.setNoRrcRiceExtensionConstraintFlag(
      m_noRrcRiceExtensionConstraintFlag);
    CHECK(m_noRrcRiceExtensionConstraintFlag && m_rrcRiceExtensionEnableFlag,
          "ExtendedRiceRRC shall be deactivated when "
          "m_noRrcRiceExtensionConstraintFlag is equal to 1");
    m_cEncLib.setNoPersistentRiceAdaptationConstraintFlag(
      m_noPersistentRiceAdaptationConstraintFlag);
    CHECK(m_noPersistentRiceAdaptationConstraintFlag
            && m_persistentRiceAdaptationEnabledFlag,
          "GolombRiceParameterAdaptation shall be deactivated when "
          "m_noPersistentRiceAdaptationConstraintFlag is equal to 1");
    m_cEncLib.setNoReverseLastSigCoeffConstraintFlag(
      m_noReverseLastSigCoeffConstraintFlag);
    CHECK(m_noReverseLastSigCoeffConstraintFlag
            && m_reverseLastSigCoeffEnabledFlag,
          "ReverseLastSigCoeff shall be deactivated when "
          "m_noReverseLastSigCoeffConstraintFlag is equal to 1");
  } else {
    m_cEncLib.setNonPackedConstraintFlag(false);
    m_cEncLib.setNonProjectedConstraintFlag(false);
    m_cEncLib.setAllLayersIndependentConstraintFlag(false);
    m_cEncLib.setNoResChangeInClvsConstraintFlag(false);
    m_cEncLib.setOneTilePerPicConstraintFlag(false);
    m_cEncLib.setPicHeaderInSliceHeaderConstraintFlag(false);
    m_cEncLib.setOneSlicePerPicConstraintFlag(false);
    m_cEncLib.setNoIdrRplConstraintFlag(false);
    m_cEncLib.setNoRectSliceConstraintFlag(false);
    m_cEncLib.setOneSlicePerSubpicConstraintFlag(false);
    m_cEncLib.setNoSubpicInfoConstraintFlag(false);
    m_cEncLib.setOnePictureOnlyConstraintFlag(false);
    m_cEncLib.setIntraOnlyConstraintFlag(false);
    m_cEncLib.setMaxBitDepthConstraintIdc(16);
    m_cEncLib.setMaxChromaFormatConstraintIdc(ChromaFormat::_444);
    m_cEncLib.setNoMttConstraintFlag(false);
    m_cEncLib.setNoQtbttDualTreeIntraConstraintFlag(false);
    m_cEncLib.setNoPartitionConstraintsOverrideConstraintFlag(false);
    m_cEncLib.setNoSaoConstraintFlag(false);
    m_cEncLib.setNoAlfConstraintFlag(false);
    m_cEncLib.setNoCCAlfConstraintFlag(false);
    m_cEncLib.setNoWeightedPredictionConstraintFlag(false);
    m_cEncLib.setNoRefWraparoundConstraintFlag(false);
    m_cEncLib.setNoTemporalMvpConstraintFlag(false);
    m_cEncLib.setNoSbtmvpConstraintFlag(false);
    m_cEncLib.setNoAmvrConstraintFlag(false);
    m_cEncLib.setNoBdofConstraintFlag(false);
    m_cEncLib.setNoDmvrConstraintFlag(false);
    m_cEncLib.setNoCclmConstraintFlag(false);
    m_cEncLib.setNoMtsConstraintFlag(false);
    m_cEncLib.setNoSbtConstraintFlag(false);
    m_cEncLib.setNoAffineMotionConstraintFlag(false);
    m_cEncLib.setNoBcwConstraintFlag(false);
    m_cEncLib.setNoIbcConstraintFlag(false);
    m_cEncLib.setNoCiipConstraintFlag(false);
    m_cEncLib.setNoGeoConstraintFlag(false);
    m_cEncLib.setNoLadfConstraintFlag(false);
    m_cEncLib.setNoTransformSkipConstraintFlag(false);
    m_cEncLib.setNoBDPCMConstraintFlag(false);
    m_cEncLib.setNoJointCbCrConstraintFlag(false);
    m_cEncLib.setNoCuQpDeltaConstraintFlag(false);
    m_cEncLib.setNoDepQuantConstraintFlag(false);
    m_cEncLib.setNoSignDataHidingConstraintFlag(false);
    m_cEncLib.setNoTrailConstraintFlag(false);
    m_cEncLib.setNoStsaConstraintFlag(false);
    m_cEncLib.setNoRaslConstraintFlag(false);
    m_cEncLib.setNoRadlConstraintFlag(false);
    m_cEncLib.setNoIdrConstraintFlag(false);
    m_cEncLib.setNoCraConstraintFlag(false);
    m_cEncLib.setNoGdrConstraintFlag(false);
    m_cEncLib.setNoApsConstraintFlag(false);
    m_cEncLib.setNoMrlConstraintFlag(false);
    m_cEncLib.setNoIspConstraintFlag(false);
    m_cEncLib.setNoMipConstraintFlag(false);
    m_cEncLib.setNoLfnstConstraintFlag(false);
    m_cEncLib.setNoMmvdConstraintFlag(false);
    m_cEncLib.setNoSmvdConstraintFlag(false);
    m_cEncLib.setNoProfConstraintFlag(false);
    m_cEncLib.setNoPaletteConstraintFlag(false);
    m_cEncLib.setNoActConstraintFlag(false);
    m_cEncLib.setNoLmcsConstraintFlag(false);
    m_cEncLib.setNoChromaQpOffsetConstraintFlag(false);
    m_cEncLib.setAllRapPicturesFlag(false);
    m_cEncLib.setNoExtendedPrecisionProcessingConstraintFlag(false);
    m_cEncLib.setNoTsResidualCodingRiceConstraintFlag(false);
    m_cEncLib.setNoRrcRiceExtensionConstraintFlag(false);
    m_cEncLib.setNoPersistentRiceAdaptationConstraintFlag(false);
    m_cEncLib.setNoReverseLastSigCoeffConstraintFlag(false);
  }

  //====== Coding Structure ========
  m_cEncLib.setIntraPeriod(m_intraPeriod);
#  if GDR_ENABLED
  m_cEncLib.setGdrEnabled(m_gdrEnabled);
  m_cEncLib.setGdrPeriod(m_gdrPeriod);
  m_cEncLib.setGdrPocStart(m_gdrPocStart);
  m_cEncLib.setGdrInterval(m_gdrInterval);
  m_cEncLib.setGdrNoHash(m_gdrNoHash);
#  endif
  m_cEncLib.setDecodingRefreshType(m_intraRefreshType);
  m_cEncLib.setGOPSize(m_gopSize);
  m_cEncLib.setDrapPeriod(m_drapPeriod);
  m_cEncLib.setEdrapPeriod(m_edrapPeriod);
  m_cEncLib.setReWriteParamSets(m_rewriteParamSets);
  m_cEncLib.setRPLList0(m_RPLList0);
  m_cEncLib.setRPLList1(m_RPLList1);
  m_cEncLib.setIDRRefParamListPresent(m_idrRefParamList);
  m_cEncLib.setGopList(m_GOPList);

  for (int i = 0; i < MAX_TLAYER; i++) {
    m_cEncLib.setMaxNumReorderPics(m_maxNumReorderPics[i], i);
    m_cEncLib.setMaxDecPicBuffering(m_maxDecPicBuffering[i], i);
  }
  for (uint32_t uiLoop = 0; uiLoop < MAX_TLAYER; ++uiLoop) {
    m_cEncLib.setLambdaModifier(uiLoop, m_adLambdaModifier[uiLoop]);
  }
  m_cEncLib.setIntraLambdaModifier(m_adIntraLambdaModifier);
  m_cEncLib.setIntraQpFactor(m_dIntraQpFactor);

  m_cEncLib.setBaseQP(m_iQP);

  m_cEncLib.setIntraQPOffset(m_intraQPOffset);
  m_cEncLib.setLambdaFromQPEnable(m_lambdaFromQPEnable);
  m_cEncLib.setChromaQpMappingTableParams(m_chromaQpMappingTableParams);

  m_cEncLib.setSourcePadding(m_sourcePadding);

  m_cEncLib.setAccessUnitDelimiter(m_AccessUnitDelimiter);
  m_cEncLib.setEnablePictureHeaderInSliceHeader(
    m_enablePictureHeaderInSliceHeader);

  m_cEncLib.setMaxTempLayer(m_maxTempLayer);
  m_cEncLib.setIsLowDelay(m_isLowDelay);

  //===== Slice ========

  //====== Loop/Deblock Filter ========
  m_cEncLib.setDeblockingFilterDisable(m_deblockingFilterDisable);
  m_cEncLib.setDeblockingFilterOffsetInPPS(m_deblockingFilterOffsetInPPS);
  m_cEncLib.setDeblockingFilterBetaOffset(m_deblockingFilterBetaOffsetDiv2);
  m_cEncLib.setDeblockingFilterTcOffset(m_deblockingFilterTcOffsetDiv2);
  m_cEncLib.setDeblockingFilterCbBetaOffset(
    m_deblockingFilterCbBetaOffsetDiv2);
  m_cEncLib.setDeblockingFilterCbTcOffset(m_deblockingFilterCbTcOffsetDiv2);
  m_cEncLib.setDeblockingFilterCrBetaOffset(
    m_deblockingFilterCrBetaOffsetDiv2);
  m_cEncLib.setDeblockingFilterCrTcOffset(m_deblockingFilterCrTcOffsetDiv2);
  m_cEncLib.setDeblockingFilterMetric(m_deblockingFilterMetric);

  //====== Motion search ========
  m_cEncLib.setDisableIntraPUsInInterSlices(m_bDisableIntraPUsInInterSlices);
  m_cEncLib.setMotionEstimationSearchMethod(m_motionEstimationSearchMethod);
  m_cEncLib.setSearchRange(m_iSearchRange);
  m_cEncLib.setBipredSearchRange(m_bipredSearchRange);
  m_cEncLib.setClipForBiPredMeEnabled(m_bClipForBiPredMeEnabled);
  m_cEncLib.setFastMEAssumingSmootherMVEnabled(
    m_bFastMEAssumingSmootherMVEnabled);
  m_cEncLib.setMinSearchWindow(m_minSearchWindow);
  m_cEncLib.setRestrictMESampling(m_bRestrictMESampling);

  //====== Quality control ========
  m_cEncLib.setMaxDeltaQP(m_iMaxDeltaQP);
  m_cEncLib.setCuQpDeltaSubdiv(m_cuQpDeltaSubdiv);
  m_cEncLib.setCuChromaQpOffsetSubdiv(m_cuChromaQpOffsetSubdiv);
  m_cEncLib.setCuChromaQpOffsetList(m_cuChromaQpOffsetList);
  m_cEncLib.setCuChromaQpOffsetEnabled(m_cuChromaQpOffsetEnabled);
  m_cEncLib.setChromaCbQpOffset(m_cbQpOffset);
  m_cEncLib.setChromaCrQpOffset(m_crQpOffset);
  m_cEncLib.setChromaCbQpOffsetDualTree(m_cbQpOffsetDualTree);
  m_cEncLib.setChromaCrQpOffsetDualTree(m_crQpOffsetDualTree);
  m_cEncLib.setChromaCbCrQpOffset(m_cbCrQpOffset);
  m_cEncLib.setChromaCbCrQpOffsetDualTree(m_cbCrQpOffsetDualTree);
#  if ER_CHROMA_QP_WCG_PPS
  m_cEncLib.setWCGChromaQpControl(m_wcgChromaQpControl);
#  endif
#  if W0038_CQP_ADJ
  m_cEncLib.setSliceChromaOffsetQpIntraOrPeriodic(
    m_sliceChromaQpOffsetPeriodicity, m_sliceChromaQpOffsetIntraOrPeriodic);
#  endif
  m_cEncLib.setChromaFormatIdc(m_chromaFormatIdc);
  m_cEncLib.setUseAdaptiveQP(m_bUseAdaptiveQP);
  m_cEncLib.setQPAdaptationRange(m_iQPAdaptationRange);
#  if ENABLE_QPA
  m_cEncLib.setUsePerceptQPA(m_bUsePerceptQPA && !m_bUseAdaptiveQP);
  m_cEncLib.setUseWPSNR(m_bUseWPSNR);
#  endif
  m_cEncLib.setExtendedPrecisionProcessingFlag(
    m_extendedPrecisionProcessingFlag);
  m_cEncLib.setRrcRiceExtensionEnableFlag(m_rrcRiceExtensionEnableFlag);
  m_cEncLib.setTSRCRicePresentFlag(m_tsrcRicePresentFlag);
  m_cEncLib.setReverseLastSigCoeffEnabledFlag(
    m_reverseLastSigCoeffEnabledFlag);
  m_cEncLib.setHighPrecisionOffsetsEnabledFlag(
    m_highPrecisionOffsetsEnabledFlag);

  m_cEncLib.setWeightedPredictionMethod(m_weightedPredictionMethod);

  //====== Tool list ========
#  if SHARP_LUMA_DELTA_QP
  m_cEncLib.setLumaLevelToDeltaQPControls(m_lumaLevelToDeltaQPMapping);
#  endif
  m_cEncLib.setSmoothQPReductionEnable(m_smoothQPReductionEnable);
  m_cEncLib.setSmoothQPReductionPeriodicity(m_smoothQPReductionPeriodicity);
  m_cEncLib.setSmoothQPReductionThresholdIntra(
    m_smoothQPReductionThresholdIntra);
  m_cEncLib.setSmoothQPReductionModelScaleIntra(
    m_smoothQPReductionModelScaleIntra);
  m_cEncLib.setSmoothQPReductionModelOffsetIntra(
    m_smoothQPReductionModelOffsetIntra);
  m_cEncLib.setSmoothQPReductionLimitIntra(m_smoothQPReductionLimitIntra);
  m_cEncLib.setSmoothQPReductionThresholdInter(
    m_smoothQPReductionThresholdInter);
  m_cEncLib.setSmoothQPReductionModelScaleInter(
    m_smoothQPReductionModelScaleInter);
  m_cEncLib.setSmoothQPReductionModelOffsetInter(
    m_smoothQPReductionModelOffsetInter);
  m_cEncLib.setSmoothQPReductionLimitInter(m_smoothQPReductionLimitInter);
  m_cEncLib.setDeltaQpRD((m_costMode == COST_LOSSLESS_CODING) ? 0
                                                              : m_uiDeltaQpRD);
  m_cEncLib.setFastDeltaQp(m_bFastDeltaQP);
  m_cEncLib.setUseASR(m_bUseASR);
  m_cEncLib.setUseHADME(m_bUseHADME);
  m_cEncLib.setdQPs(m_frameDeltaQps);
  m_cEncLib.setUseRDOQ(m_useRDOQ);
  m_cEncLib.setUseRDOQTS(m_useRDOQTS);
  m_cEncLib.setUseSelectiveRDOQ(m_useSelectiveRDOQ);
  m_cEncLib.setRDpenalty(m_rdPenalty);
  m_cEncLib.setCTUSize(m_ctuSize);
  m_cEncLib.setSubPicInfoPresentFlag(m_subPicInfoPresentFlag);
  if (m_subPicInfoPresentFlag) {
    m_cEncLib.setNumSubPics(m_numSubPics);
    m_cEncLib.setSubPicSameSizeFlag(m_subPicSameSizeFlag);
    m_cEncLib.setSubPicCtuTopLeftX(m_subPicCtuTopLeftX);
    m_cEncLib.setSubPicCtuTopLeftY(m_subPicCtuTopLeftY);
    m_cEncLib.setSubPicWidth(m_subPicWidth);
    m_cEncLib.setSubPicHeight(m_subPicHeight);
    m_cEncLib.setSubPicTreatedAsPicFlag(m_subPicTreatedAsPicFlag);
    m_cEncLib.setLoopFilterAcrossSubpicEnabledFlag(
      m_loopFilterAcrossSubpicEnabledFlag);
    m_cEncLib.setSubPicIdMappingInSpsFlag(m_subPicIdMappingInSpsFlag);
    m_cEncLib.setSubPicIdLen(m_subPicIdLen);
    m_cEncLib.setSubPicIdMappingExplicitlySignalledFlag(
      m_subPicIdMappingExplicitlySignalledFlag);
    if (m_subPicIdMappingExplicitlySignalledFlag) {
      m_cEncLib.setSubPicId(m_subPicId);
    }
  } else {
    m_cEncLib.setNumSubPics(1);
    m_cEncLib.setSubPicIdMappingExplicitlySignalledFlag(false);
  }

  m_cEncLib.setUseSplitConsOverride(m_SplitConsOverrideEnabledFlag);
  // convert the Intra Chroma minQT setting from chroma unit to luma unit
  m_minQt[2] <<= getChannelTypeScaleX(ChannelType::CHROMA, m_chromaFormatIdc);
  m_cEncLib.setMinQTSizes(m_minQt);
  m_cEncLib.setMaxMTTHierarchyDepth(m_uiMaxMTTHierarchyDepth,
                                    m_uiMaxMTTHierarchyDepthI,
                                    m_uiMaxMTTHierarchyDepthIChroma);
  m_cEncLib.setMaxBTSizes(m_maxBt);
  m_cEncLib.setMaxTTSizes(m_maxTt);
  m_cEncLib.setFastTTskip(m_ttFastSkip);
  m_cEncLib.setFastTTskipThr(m_ttFastSkipThr);
  m_cEncLib.setDualITree(m_dualTree);
  m_cEncLib.setLFNST(m_LFNST);
  m_cEncLib.setUseFastLFNST(m_useFastLFNST);
  m_cEncLib.setSbTmvpEnabledFlag(m_sbTmvpEnableFlag);
  m_cEncLib.setAffine(m_Affine);
  m_cEncLib.setAffineType(m_AffineType);
  m_cEncLib.setAdaptBypassAffineMe(m_adaptBypassAffineMe);
  m_cEncLib.setPROF(m_PROF);
  m_cEncLib.setBIO(m_BIO);
  m_cEncLib.setUseLMChroma(m_LMChroma);
  m_cEncLib.setHorCollocatedChromaFlag(m_horCollocatedChromaFlag != 0);
  m_cEncLib.setVerCollocatedChromaFlag(m_verCollocatedChromaFlag != 0);
  m_cEncLib.setExplicitMtsIntraEnabled((m_mtsMode & 1) != 0);
  m_cEncLib.setExplicitMtsInterEnabled((m_mtsMode & 2) != 0);
  m_cEncLib.setMTSIntraMaxCand(m_MTSIntraMaxCand);
  m_cEncLib.setMTSInterMaxCand(m_MTSInterMaxCand);
  m_cEncLib.setImplicitMtsIntraEnabled(m_mtsImplicitIntra
                                       || (m_mtsMode & 4) != 0);
  m_cEncLib.setUseSBT(m_SBT);
  m_cEncLib.setSBTFast64WidthTh(m_SBTFast64WidthTh);
  m_cEncLib.setUseCompositeRef(m_compositeRefEnabled);
  m_cEncLib.setUseSMVD(m_SMVD);
  m_cEncLib.setUseBcw(m_bcw);
  m_cEncLib.setUseBcwFast(m_BcwFast);
  m_cEncLib.setUseLadf(m_LadfEnabed);
  if (m_LadfEnabed) {
    m_cEncLib.setLadfNumIntervals(m_ladfNumIntervals);
    for (int k = 0; k < m_ladfNumIntervals; k++) {
      m_cEncLib.setLadfQpOffset(m_ladfQpOffset[k], k);
      m_cEncLib.setLadfIntervalLowerBound(m_ladfIntervalLowerBound[k], k);
    }
  }
  if (m_rprFunctionalityTestingEnabledFlag) {
    for (int k = 0; k < m_rprSwitchingListSize; k++) {
      m_cEncLib.setRprSwitchingResolutionOrderList(
        m_rprSwitchingResolutionOrderList[k], k);
      m_cEncLib.setRprSwitchingQPOffsetOrderList(
        m_rprSwitchingQPOffsetOrderList[k], k);
    }
    m_cEncLib.setRprSwitchingListSize(m_rprSwitchingListSize);
  }
  m_cEncLib.setUseCiip(m_ciip);
  m_cEncLib.setUseGeo(m_Geo);
  m_cEncLib.setUseHashMECfgEnable(m_HashME);
  m_cEncLib.setAllowDisFracMMVD(m_allowDisFracMMVD);
  m_cEncLib.setUseAffineAmvr(m_AffineAmvr);
  m_cEncLib.setUseAffineAmvrEncOpt(m_AffineAmvrEncOpt);
  m_cEncLib.setUseAffineAmvp(m_AffineAmvp);
  m_cEncLib.setDMVR(m_DMVR);
  m_cEncLib.setMMVD(m_MMVD);
  m_cEncLib.setMmvdDisNum(m_MmvdDisNum);
  m_cEncLib.setRGBFormatFlag(m_rgbFormat);
  m_cEncLib.setUseColorTrans(m_useColorTrans);
  m_cEncLib.setPLTMode(m_PLTMode);
  m_cEncLib.setJointCbCr(m_jointCbCrMode);
  m_cEncLib.setIBCMode(m_IBCMode);
  m_cEncLib.setIBCLocalSearchRangeX(m_IBCLocalSearchRangeX);
  m_cEncLib.setIBCLocalSearchRangeY(m_IBCLocalSearchRangeY);
  m_cEncLib.setIBCHashSearch(m_IBCHashSearch);
  m_cEncLib.setIBCHashSearchMaxCand(m_IBCHashSearchMaxCand);
  m_cEncLib.setIBCHashSearchRange4SmallBlk(m_IBCHashSearchRange4SmallBlk);
  m_cEncLib.setIBCFastMethod(m_IBCFastMethod);
#  if JVET_AD0045
  if (m_dmvrEncSelect && (m_iQP >= m_dmvrEncSelectBaseQpTh)) {
    m_cEncLib.setDMVREncMvSelection(true);
  } else {
    m_cEncLib.setDMVREncMvSelection(false);
  }
  m_cEncLib.setDMVREncMvSelectDisableHighestTemporalLayer(
    m_dmvrEncSelectDisableHighestTemporalLayer);
#  endif

  m_cEncLib.setUseWrapAround(m_wrapAround);
  m_cEncLib.setWrapAroundOffset(m_wrapAroundOffset);

  // ADD_NEW_TOOL : (encoder app) add setting of tool enabling flags and associated parameters here
  m_cEncLib.setVirtualBoundariesEnabledFlag(m_virtualBoundariesEnabledFlag);
  if (m_cEncLib.getVirtualBoundariesEnabledFlag()) {
    m_cEncLib.setVirtualBoundariesPresentFlag(m_virtualBoundariesPresentFlag);
    m_cEncLib.setNumVerVirtualBoundaries(m_numVerVirtualBoundaries);
    m_cEncLib.setNumHorVirtualBoundaries(m_numHorVirtualBoundaries);
    for (unsigned i = 0; i < m_numVerVirtualBoundaries; i++) {
      m_cEncLib.setVirtualBoundariesPosX(m_virtualBoundariesPosX[i], i);
    }
    for (unsigned i = 0; i < m_numHorVirtualBoundaries; i++) {
      m_cEncLib.setVirtualBoundariesPosY(m_virtualBoundariesPosY[i], i);
    }
  }

  m_cEncLib.setMaxCUWidth(m_ctuSize);
  m_cEncLib.setMaxCUHeight(m_ctuSize);
  m_cEncLib.setLog2MinCodingBlockSize(m_log2MinCuSize);
  m_cEncLib.setLog2MaxTbSize(m_log2MaxTbSize);
  m_cEncLib.setUseEncDbOpt(m_encDbOpt);
  m_cEncLib.setUseFastLCTU(m_useFastLCTU);
  m_cEncLib.setFastInterSearchMode(m_fastInterSearchMode);
  m_cEncLib.setUseEarlyCU(m_bUseEarlyCU);
  m_cEncLib.setUseFastDecisionForMerge(m_useFastDecisionForMerge);
  m_cEncLib.setUseEarlySkipDetection(m_useEarlySkipDetection);
  m_cEncLib.setUseFastMerge(m_useFastMrg);
  m_cEncLib.setMaxMergeRdCandNumTotal(m_maxMergeRdCandNumTotal);
  m_cEncLib.setMergeRdCandQuotaRegular(m_mergeRdCandQuotaRegular);
  m_cEncLib.setMergeRdCandQuotaRegularSmallBlk(
    m_mergeRdCandQuotaRegularSmallBlk);
  m_cEncLib.setMergeRdCandQuotaSubBlk(m_mergeRdCandQuotaSubBlk);
  m_cEncLib.setMergeRdCandQuotaCiip(m_mergeRdCandQuotaCiip);
  m_cEncLib.setMergeRdCandQuotaGpm(m_mergeRdCandQuotaGpm);
  m_cEncLib.setUsePbIntraFast(m_usePbIntraFast);
  m_cEncLib.setUseAMaxBT(m_useAMaxBT);
  m_cEncLib.setUseE0023FastEnc(m_e0023FastEnc);
  m_cEncLib.setUseContentBasedFastQtbt(m_contentBasedFastQtbt);
  m_cEncLib.setUseNonLinearAlfLuma(m_useNonLinearAlfLuma);
  m_cEncLib.setUseNonLinearAlfChroma(m_useNonLinearAlfChroma);
  m_cEncLib.setMaxNumAlfAlternativesChroma(m_maxNumAlfAlternativesChroma);
  m_cEncLib.setUseMRL(m_MRL);
  m_cEncLib.setUseMIP(m_MIP);
  m_cEncLib.setUseFastMIP(m_useFastMIP);
  m_cEncLib.setFastLocalDualTreeMode(m_fastLocalDualTreeMode);
  m_cEncLib.setUseReconBasedCrossCPredictionEstimate(
    m_reconBasedCrossCPredictionEstimate);
  m_cEncLib.setUseTransformSkip(m_useTransformSkip);
  m_cEncLib.setUseTransformSkipFast(m_useTransformSkipFast);
  m_cEncLib.setUseChromaTS(m_useChromaTS && m_useTransformSkip);
  m_cEncLib.setUseBDPCM(m_useBDPCM);
  m_cEncLib.setTransformSkipRotationEnabledFlag(
    m_transformSkipRotationEnabledFlag);
  m_cEncLib.setTransformSkipContextEnabledFlag(
    m_transformSkipContextEnabledFlag);
  m_cEncLib.setRrcRiceExtensionEnableFlag(m_rrcRiceExtensionEnableFlag);
  m_cEncLib.setPersistentRiceAdaptationEnabledFlag(
    m_persistentRiceAdaptationEnabledFlag);
  m_cEncLib.setCabacBypassAlignmentEnabledFlag(
    m_cabacBypassAlignmentEnabledFlag);
  m_cEncLib.setLog2MaxTransformSkipBlockSize(m_log2MaxTransformSkipBlockSize);
  m_cEncLib.setFastUDIUseMPMEnabled(m_bFastUDIUseMPMEnabled);
  m_cEncLib.setFastMEForGenBLowDelayEnabled(m_bFastMEForGenBLowDelayEnabled);
  m_cEncLib.setUseBLambdaForNonKeyLowDelayPictures(
    m_bUseBLambdaForNonKeyLowDelayPictures);
  m_cEncLib.setUseISP(m_ISP);
  m_cEncLib.setUseFastISP(m_useFastISP);
  m_cEncLib.setFastAdaptCostPredMode(m_fastAdaptCostPredMode);
  m_cEncLib.setDisableFastDecisionTT(m_disableFastDecisionTT);

  // set internal bit-depth and constants
  for (const auto channelType : {ChannelType::LUMA, ChannelType::CHROMA}) {
    m_cEncLib.setBitDepth(channelType, m_internalBitDepth[channelType]);
    m_cEncLib.setInputBitDepth(channelType, m_inputBitDepth[channelType]);
  }

  m_cEncLib.setMaxNumMergeCand(m_maxNumMergeCand);
  m_cEncLib.setMaxNumAffineMergeCand(m_maxNumAffineMergeCand);
  m_cEncLib.setMaxNumGeoCand(m_maxNumGeoCand);
  m_cEncLib.setMaxNumIBCMergeCand(m_maxNumIBCMergeCand);

  //====== Weighted Prediction ========
  m_cEncLib.setUseWP(m_useWeightedPred);
  m_cEncLib.setWPBiPred(m_useWeightedBiPred);

  //====== Parallel Merge Estimation ========
  m_cEncLib.setLog2ParallelMergeLevelMinus2(m_log2ParallelMergeLevel - 2);
  m_cEncLib.setMixedLossyLossless(m_mixedLossyLossless);
  m_cEncLib.setSliceLosslessArray(m_sliceLosslessArray);

  //====== Tiles and Slices ========
  m_cEncLib.setNoPicPartitionFlag(!m_picPartitionFlag);
  if (m_picPartitionFlag) {
    m_cEncLib.setTileColWidths(m_tileColumnWidth);
    m_cEncLib.setTileRowHeights(m_tileRowHeight);
    m_cEncLib.setRectSliceFlag(!m_rasterSliceFlag);
    m_cEncLib.setNumSlicesInPic(m_numSlicesInPic);
    m_cEncLib.setTileIdxDeltaPresentFlag(m_tileIdxDeltaPresentFlag);
    m_cEncLib.setRectSlices(m_rectSlices);
    m_cEncLib.setRasterSliceSizes(m_rasterSliceSize);
    m_cEncLib.setLFCrossTileBoundaryFlag(!m_disableLFCrossTileBoundaryFlag);
    m_cEncLib.setLFCrossSliceBoundaryFlag(!m_disableLFCrossSliceBoundaryFlag);
  } else {
    m_cEncLib.setRectSliceFlag(true);
    m_cEncLib.setNumSlicesInPic(1);
    m_cEncLib.setTileIdxDeltaPresentFlag(0);
    m_cEncLib.setLFCrossTileBoundaryFlag(true);
    m_cEncLib.setLFCrossSliceBoundaryFlag(true);
  }

  //====== Sub-picture and Slices ========
  m_cEncLib.setSingleSlicePerSubPicFlagFlag(m_singleSlicePerSubPicFlag);
  m_cEncLib.setUseSAO(m_useSao);
  m_cEncLib.setSaoTrueOrg(m_saoTrueOrg);
  m_cEncLib.setTestSAODisableAtPictureLevel(m_bTestSAODisableAtPictureLevel);
  m_cEncLib.setSaoEncodingRate(m_saoEncodingRate);
  m_cEncLib.setSaoEncodingRateChroma(m_saoEncodingRateChroma);
  m_cEncLib.setMaxNumOffsetsPerPic(m_maxNumOffsetsPerPic);

  m_cEncLib.setSaoCtuBoundary(m_saoCtuBoundary);

  m_cEncLib.setSaoGreedyMergeEnc(m_saoGreedyMergeEnc);
  m_cEncLib.setDecodedPictureHashSEIType(m_decodedPictureHashSEIType);
  m_cEncLib.setSubpicDecodedPictureHashType(m_subpicDecodedPictureHashType);
  m_cEncLib.setDependentRAPIndicationSEIEnabled(m_drapPeriod > 0);
  m_cEncLib.setEdrapIndicationSEIEnabled(m_edrapPeriod > 0);
  m_cEncLib.setBufferingPeriodSEIEnabled(m_bufferingPeriodSEIEnabled);
  m_cEncLib.setPictureTimingSEIEnabled(m_pictureTimingSEIEnabled);
  m_cEncLib.setFrameFieldInfoSEIEnabled(m_frameFieldInfoSEIEnabled);
  m_cEncLib.setBpDeltasGOPStructure(m_bpDeltasGOPStructure);
  m_cEncLib.setDecodingUnitInfoSEIEnabled(m_decodingUnitInfoSEIEnabled);
  m_cEncLib.setScalableNestingSEIEnabled(m_scalableNestingSEIEnabled);
  m_cEncLib.setHrdParametersPresentFlag(m_hrdParametersPresentFlag);
  m_cEncLib.setFramePackingArrangementSEIEnabled(m_framePackingSEIEnabled);
  m_cEncLib.setFramePackingArrangementSEIType(m_framePackingSEIType);
  m_cEncLib.setFramePackingArrangementSEIId(m_framePackingSEIId);
  m_cEncLib.setFramePackingArrangementSEIQuincunx(m_framePackingSEIQuincunx);
  m_cEncLib.setFramePackingArrangementSEIInterpretation(
    m_framePackingSEIInterpretation);
  m_cEncLib.setDoSEIEnabled(m_doSEIEnabled);
  m_cEncLib.setDoSEICancelFlag(m_doSEICancelFlag);
  m_cEncLib.setDoSEIPersistenceFlag(m_doSEIPersistenceFlag);
  m_cEncLib.setDoSEITransformType(m_doSEITransformType);
  m_cEncLib.setParameterSetsInclusionIndicationSEIEnabled(
    m_parameterSetsInclusionIndicationSEIEnabled);
  m_cEncLib.setSelfContainedClvsFlag(m_selfContainedClvsFlag);
#  if GREEN_METADATA_SEI_ENABLED
  m_cEncLib.setGMFAFile(m_GMFAFile);
  m_cEncLib.setSEIGreenMetadataInfoSEIEnable(m_greenMetadataType);
  m_cEncLib.setSEIGreenMetadataExtendedRepresentation(
    m_greenMetadataExtendedRepresentation);
  m_cEncLib.setSEIGreenMetadataGranularityType(m_greenMetadataGranularityType);
  m_cEncLib.setSEIGreenMetadataType(m_greenMetadataType);
  m_cEncLib.setSEIGreenMetadataPeriodType(m_greenMetadataPeriodType);
  m_cEncLib.setSEIGreenMetadataPeriodNumPictures(
    m_greenMetadataPeriodNumPictures);
  m_cEncLib.setSEIGreenMetadataPeriodNumSeconds(
    m_greenMetadataPeriodNumSeconds);
  //Metrics for quality recovery after low-power encoding
  m_cEncLib.setSEIXSDNumberMetrics(m_xsdNumberMetrics);
  m_cEncLib.setSEIXSDMetricTypePSNR(m_xsdMetricTypePSNR);
  m_cEncLib.setSEIXSDMetricTypeSSIM(m_xsdMetricTypeSSIM);
  m_cEncLib.setSEIXSDMetricTypeWPSNR(m_xsdMetricTypeWPSNR);
  m_cEncLib.setSEIXSDMetricTypeWSPSNR(m_xsdMetricTypeWSPSNR);
#  endif
  m_cEncLib.setErpSEIEnabled(m_erpSEIEnabled);
  m_cEncLib.setErpSEICancelFlag(m_erpSEICancelFlag);
  m_cEncLib.setErpSEIPersistenceFlag(m_erpSEIPersistenceFlag);
  m_cEncLib.setErpSEIGuardBandFlag(m_erpSEIGuardBandFlag);
  m_cEncLib.setErpSEIGuardBandType(m_erpSEIGuardBandType);
  m_cEncLib.setErpSEILeftGuardBandWidth(m_erpSEILeftGuardBandWidth);
  m_cEncLib.setErpSEIRightGuardBandWidth(m_erpSEIRightGuardBandWidth);
  m_cEncLib.setSphereRotationSEIEnabled(m_sphereRotationSEIEnabled);
  m_cEncLib.setSphereRotationSEICancelFlag(m_sphereRotationSEICancelFlag);
  m_cEncLib.setSphereRotationSEIPersistenceFlag(
    m_sphereRotationSEIPersistenceFlag);
  m_cEncLib.setSphereRotationSEIYaw(m_sphereRotationSEIYaw);
  m_cEncLib.setSphereRotationSEIPitch(m_sphereRotationSEIPitch);
  m_cEncLib.setSphereRotationSEIRoll(m_sphereRotationSEIRoll);
  m_cEncLib.setOmniViewportSEIEnabled(m_omniViewportSEIEnabled);
  m_cEncLib.setOmniViewportSEIId(m_omniViewportSEIId);
  m_cEncLib.setOmniViewportSEICancelFlag(m_omniViewportSEICancelFlag);
  m_cEncLib.setOmniViewportSEIPersistenceFlag(
    m_omniViewportSEIPersistenceFlag);
  m_cEncLib.setOmniViewportSEICntMinus1(m_omniViewportSEICntMinus1);
  m_cEncLib.setOmniViewportSEIAzimuthCentre(m_omniViewportSEIAzimuthCentre);
  m_cEncLib.setOmniViewportSEIElevationCentre(
    m_omniViewportSEIElevationCentre);
  m_cEncLib.setOmniViewportSEITiltCentre(m_omniViewportSEITiltCentre);
  m_cEncLib.setOmniViewportSEIHorRange(m_omniViewportSEIHorRange);
  m_cEncLib.setOmniViewportSEIVerRange(m_omniViewportSEIVerRange);
  m_cEncLib.setAnnotatedRegionSEIFileRoot(m_arSEIFileRoot);
  m_cEncLib.setRwpSEIEnabled(m_rwpSEIEnabled);
  m_cEncLib.setRwpSEIRwpCancelFlag(m_rwpSEIRwpCancelFlag);
  m_cEncLib.setRwpSEIRwpPersistenceFlag(m_rwpSEIRwpPersistenceFlag);
  m_cEncLib.setRwpSEIConstituentPictureMatchingFlag(
    m_rwpSEIConstituentPictureMatchingFlag);
  m_cEncLib.setRwpSEINumPackedRegions(m_rwpSEINumPackedRegions);
  m_cEncLib.setRwpSEIProjPictureWidth(m_rwpSEIProjPictureWidth);
  m_cEncLib.setRwpSEIProjPictureHeight(m_rwpSEIProjPictureHeight);
  m_cEncLib.setRwpSEIPackedPictureWidth(m_rwpSEIPackedPictureWidth);
  m_cEncLib.setRwpSEIPackedPictureHeight(m_rwpSEIPackedPictureHeight);
  m_cEncLib.setRwpSEIRwpTransformType(m_rwpSEIRwpTransformType);
  m_cEncLib.setRwpSEIRwpGuardBandFlag(m_rwpSEIRwpGuardBandFlag);
  m_cEncLib.setRwpSEIProjRegionWidth(m_rwpSEIProjRegionWidth);
  m_cEncLib.setRwpSEIProjRegionHeight(m_rwpSEIProjRegionHeight);
  m_cEncLib.setRwpSEIRwpSEIProjRegionTop(m_rwpSEIRwpSEIProjRegionTop);
  m_cEncLib.setRwpSEIProjRegionLeft(m_rwpSEIProjRegionLeft);
  m_cEncLib.setRwpSEIPackedRegionWidth(m_rwpSEIPackedRegionWidth);
  m_cEncLib.setRwpSEIPackedRegionHeight(m_rwpSEIPackedRegionHeight);
  m_cEncLib.setRwpSEIPackedRegionTop(m_rwpSEIPackedRegionTop);
  m_cEncLib.setRwpSEIPackedRegionLeft(m_rwpSEIPackedRegionLeft);
  m_cEncLib.setRwpSEIRwpLeftGuardBandWidth(m_rwpSEIRwpLeftGuardBandWidth);
  m_cEncLib.setRwpSEIRwpRightGuardBandWidth(m_rwpSEIRwpRightGuardBandWidth);
  m_cEncLib.setRwpSEIRwpTopGuardBandHeight(m_rwpSEIRwpTopGuardBandHeight);
  m_cEncLib.setRwpSEIRwpBottomGuardBandHeight(
    m_rwpSEIRwpBottomGuardBandHeight);
  m_cEncLib.setRwpSEIRwpGuardBandNotUsedForPredFlag(
    m_rwpSEIRwpGuardBandNotUsedForPredFlag);
  m_cEncLib.setRwpSEIRwpGuardBandType(m_rwpSEIRwpGuardBandType);
  m_cEncLib.setGcmpSEIEnabled(m_gcmpSEIEnabled);
  m_cEncLib.setGcmpSEICancelFlag(m_gcmpSEICancelFlag);
  m_cEncLib.setGcmpSEIPersistenceFlag(m_gcmpSEIPersistenceFlag);
  m_cEncLib.setGcmpSEIPackingType((uint8_t)m_gcmpSEIPackingType);
  m_cEncLib.setGcmpSEIMappingFunctionType(
    (uint8_t)m_gcmpSEIMappingFunctionType);
  m_cEncLib.setGcmpSEIFaceIndex(m_gcmpSEIFaceIndex);
  m_cEncLib.setGcmpSEIFaceRotation(m_gcmpSEIFaceRotation);
  m_cEncLib.setGcmpSEIFunctionCoeffU(m_gcmpSEIFunctionCoeffU);
  m_cEncLib.setGcmpSEIFunctionUAffectedByVFlag(
    m_gcmpSEIFunctionUAffectedByVFlag);
  m_cEncLib.setGcmpSEIFunctionCoeffV(m_gcmpSEIFunctionCoeffV);
  m_cEncLib.setGcmpSEIFunctionVAffectedByUFlag(
    m_gcmpSEIFunctionVAffectedByUFlag);
  m_cEncLib.setGcmpSEIGuardBandFlag(m_gcmpSEIGuardBandFlag);
  m_cEncLib.setGcmpSEIGuardBandType(m_gcmpSEIGuardBandType);
  m_cEncLib.setGcmpSEIGuardBandBoundaryExteriorFlag(
    m_gcmpSEIGuardBandBoundaryExteriorFlag);
  m_cEncLib.setGcmpSEIGuardBandSamplesMinus1(
    (uint8_t)m_gcmpSEIGuardBandSamplesMinus1);
  m_cEncLib.setSubpicureLevelInfoSEICfg(m_cfgSubpictureLevelInfoSEI);
  m_cEncLib.setSampleAspectRatioInfoSEIEnabled(
    m_sampleAspectRatioInfoSEIEnabled);
  m_cEncLib.setSariCancelFlag(m_sariCancelFlag);
  m_cEncLib.setSariPersistenceFlag(m_sariPersistenceFlag);
  m_cEncLib.setSariAspectRatioIdc(m_sariAspectRatioIdc);
  m_cEncLib.setSariSarWidth(m_sariSarWidth);
  m_cEncLib.setSariSarHeight(m_sariSarHeight);
  m_cEncLib.setPhaseIndicationSEIEnabledFullResolution(
    m_phaseIndicationSEIEnabledFullResolution);
  m_cEncLib.setHorPhaseNumFullResolution(m_piHorPhaseNumFullResolution);
  m_cEncLib.setHorPhaseDenMinus1FullResolution(
    m_piHorPhaseDenMinus1FullResolution);
  m_cEncLib.setVerPhaseNumFullResolution(m_piVerPhaseNumFullResolution);
  m_cEncLib.setVerPhaseDenMinus1FullResolution(
    m_piVerPhaseDenMinus1FullResolution);
  m_cEncLib.setPhaseIndicationSEIEnabledReducedResolution(
    m_phaseIndicationSEIEnabledReducedResolution);
  m_cEncLib.setHorPhaseNumReducedResolution(m_piHorPhaseNumReducedResolution);
  m_cEncLib.setHorPhaseDenMinus1ReducedResolution(
    m_piHorPhaseDenMinus1ReducedResolution);
  m_cEncLib.setVerPhaseNumReducedResolution(m_piVerPhaseNumReducedResolution);
  m_cEncLib.setVerPhaseDenMinus1ReducedResolution(
    m_piVerPhaseDenMinus1ReducedResolution);
  m_cEncLib.setMCTSEncConstraint(m_MCTSEncConstraint);
  m_cEncLib.setMasteringDisplaySEI(m_masteringDisplay);
  m_cEncLib.setSEIAlternativeTransferCharacteristicsSEIEnable(
    m_preferredTransferCharacteristics >= 0);
  m_cEncLib.setSEIPreferredTransferCharacteristics(
    uint8_t(m_preferredTransferCharacteristics));
  // film grain charcteristics
  m_cEncLib.setFilmGrainCharactersticsSEIEnabled(m_fgcSEIEnabled);
  m_cEncLib.setFilmGrainCharactersticsSEICancelFlag(m_fgcSEICancelFlag);
  m_cEncLib.setFilmGrainCharactersticsSEIPersistenceFlag(
    m_fgcSEIPersistenceFlag);
  m_cEncLib.setFilmGrainCharactersticsSEIModelID((uint8_t)m_fgcSEIModelID);
  m_cEncLib.setFilmGrainCharactersticsSEISepColourDescPresent(
    m_fgcSEISepColourDescPresentFlag);
  m_cEncLib.setFilmGrainCharactersticsSEIBlendingModeID(
    (uint8_t)m_fgcSEIBlendingModeID);
  m_cEncLib.setFilmGrainCharactersticsSEILog2ScaleFactor(
    (uint8_t)m_fgcSEILog2ScaleFactor);
  m_cEncLib.setFilmGrainAnalysisEnabled(m_fgcSEIAnalysisEnabled);
  m_cEncLib.setFilmGrainExternalMask(m_fgcSEIExternalMask);
  m_cEncLib.setFilmGrainExternalDenoised(m_fgcSEIExternalDenoised);
  m_cEncLib.setFilmGrainTemporalFilterPastRefs(m_fgcSEITemporalFilterPastRefs);
  m_cEncLib.setFilmGrainTemporalFilterFutureRefs(
    m_fgcSEITemporalFilterFutureRefs);
  m_cEncLib.setFilmGrainTemporalFilterStrengths(
    m_fgcSEITemporalFilterStrengths);
  m_cEncLib.setFilmGrainCharactersticsSEIPerPictureSEI(m_fgcSEIPerPictureSEI);
  for (int i = 0; i < MAX_NUM_COMPONENT; i++) {
    m_cEncLib.setFGCSEICompModelPresent(m_fgcSEICompModelPresent[i], i);
    if (m_fgcSEICompModelPresent[i]) {
      m_cEncLib.setFGCSEINumIntensityIntervalMinus1(
        (uint8_t)m_fgcSEINumIntensityIntervalMinus1[i], i);
      m_cEncLib.setFGCSEINumModelValuesMinus1(
        (uint8_t)m_fgcSEINumModelValuesMinus1[i], i);
      for (int j = 0; j <= m_fgcSEINumIntensityIntervalMinus1[i]; j++) {
        m_cEncLib.setFGCSEIIntensityIntervalLowerBound(
          (uint8_t)m_fgcSEIIntensityIntervalLowerBound[i][j], i, j);
        m_cEncLib.setFGCSEIIntensityIntervalUpperBound(
          (uint8_t)m_fgcSEIIntensityIntervalUpperBound[i][j], i, j);
        for (int k = 0; k <= m_fgcSEINumModelValuesMinus1[i]; k++) {
          m_cEncLib.setFGCSEICompModelValue(
            m_fgcSEICompModelValue[i][j][k], i, j, k);
        }
      }
    }
  }
  // content light level
  m_cEncLib.setCLLSEIEnabled(m_cllSEIEnabled);
  m_cEncLib.setCLLSEIMaxContentLightLevel((uint16_t)m_cllSEIMaxContentLevel);
  m_cEncLib.setCLLSEIMaxPicAvgLightLevel((uint16_t)m_cllSEIMaxPicAvgLevel);
  // ambient viewing enviornment
  m_cEncLib.setAmbientViewingEnvironmentSEIEnabled(m_aveSEIEnabled);
  m_cEncLib.setAmbientViewingEnvironmentSEIIlluminance(
    m_aveSEIAmbientIlluminance);
  m_cEncLib.setAmbientViewingEnvironmentSEIAmbientLightX(
    (uint16_t)m_aveSEIAmbientLightX);
  m_cEncLib.setAmbientViewingEnvironmentSEIAmbientLightY(
    (uint16_t)m_aveSEIAmbientLightY);
  // colour tranform information sei
  m_cEncLib.setCtiSEIEnabled(m_ctiSEIEnabled);
  m_cEncLib.setCtiSEIId(m_ctiSEIId);
  m_cEncLib.setCtiSEISignalInfoFlag(m_ctiSEISignalInfoFlag);
  m_cEncLib.setCtiSEIFullRangeFlag(m_ctiSEIFullRangeFlag);
  m_cEncLib.setCtiSEIPrimaries(m_ctiSEIPrimaries);
  m_cEncLib.setCtiSEITransferFunction(m_ctiSEITransferFunction);
  m_cEncLib.setCtiSEIMatrixCoefs(m_ctiSEIMatrixCoefs);
  m_cEncLib.setCtiSEICrossComponentFlag(m_ctiSEICrossComponentFlag);
  m_cEncLib.setCtiSEICrossComponentInferred(m_ctiSEICrossComponentInferred);
  m_cEncLib.setCtiSEINbChromaLut(m_ctiSEINumberChromaLut);
  m_cEncLib.setCtiSEIChromaOffset(m_ctiSEIChromaOffset);
  for (int i = 0; i < MAX_NUM_COMPONENT; i++) {
    m_cEncLib.setCtiSEILut(m_ctiSEILut[i], i);
  }
  // content colour volume SEI
  m_cEncLib.setCcvSEIEnabled(m_ccvSEIEnabled);
  m_cEncLib.setCcvSEICancelFlag(m_ccvSEICancelFlag);
  m_cEncLib.setCcvSEIPersistenceFlag(m_ccvSEIPersistenceFlag);
  m_cEncLib.setCcvSEIEnabled(m_ccvSEIEnabled);
  m_cEncLib.setCcvSEICancelFlag(m_ccvSEICancelFlag);
  m_cEncLib.setCcvSEIPersistenceFlag(m_ccvSEIPersistenceFlag);
  m_cEncLib.setCcvSEIPrimariesPresentFlag(m_ccvSEIPrimariesPresentFlag);
  m_cEncLib.setCcvSEIMinLuminanceValuePresentFlag(
    m_ccvSEIMinLuminanceValuePresentFlag);
  m_cEncLib.setCcvSEIMaxLuminanceValuePresentFlag(
    m_ccvSEIMaxLuminanceValuePresentFlag);
  m_cEncLib.setCcvSEIAvgLuminanceValuePresentFlag(
    m_ccvSEIAvgLuminanceValuePresentFlag);
  for (int i = 0; i < MAX_NUM_COMPONENT; i++) {
    m_cEncLib.setCcvSEIPrimariesX(m_ccvSEIPrimariesX[i], i);
    m_cEncLib.setCcvSEIPrimariesY(m_ccvSEIPrimariesY[i], i);
  }
  m_cEncLib.setCcvSEIMinLuminanceValue(m_ccvSEIMinLuminanceValue);
  m_cEncLib.setCcvSEIMaxLuminanceValue(m_ccvSEIMaxLuminanceValue);
  m_cEncLib.setCcvSEIAvgLuminanceValue(m_ccvSEIAvgLuminanceValue);
  // scalability dimension information sei
  m_cEncLib.setSdiSEIEnabled(m_sdiSEIEnabled);
  m_cEncLib.setSdiSEIMaxLayersMinus1(m_sdiSEIMaxLayersMinus1);
  m_cEncLib.setSdiSEIMultiviewInfoFlag(m_sdiSEIMultiviewInfoFlag);
  m_cEncLib.setSdiSEIAuxiliaryInfoFlag(m_sdiSEIAuxiliaryInfoFlag);
  m_cEncLib.setSdiSEIViewIdLenMinus1(m_sdiSEIViewIdLenMinus1);
  m_cEncLib.setSdiSEILayerId(m_sdiSEILayerId);
  m_cEncLib.setSdiSEIViewIdVal(m_sdiSEIViewIdVal);
  m_cEncLib.setSdiSEIAuxId(m_sdiSEIAuxId);
  m_cEncLib.setSdiSEINumAssociatedPrimaryLayersMinus1(
    m_sdiSEINumAssociatedPrimaryLayersMinus1);
  // multiview acquisition information sei
  m_cEncLib.setMaiSEIEnabled(m_maiSEIEnabled);
  m_cEncLib.setMaiSEIIntrinsicParamFlag(m_maiSEIIntrinsicParamFlag);
  m_cEncLib.setMaiSEIExtrinsicParamFlag(m_maiSEIExtrinsicParamFlag);
  m_cEncLib.setMaiSEINumViewsMinus1(m_maiSEINumViewsMinus1);
  m_cEncLib.setMaiSEIIntrinsicParamsEqualFlag(
    m_maiSEIIntrinsicParamsEqualFlag);
  m_cEncLib.setMaiSEIPrecFocalLength(m_maiSEIPrecFocalLength);
  m_cEncLib.setMaiSEIPrecPrincipalPoint(m_maiSEIPrecPrincipalPoint);
  m_cEncLib.setMaiSEIPrecSkewFactor(m_maiSEIPrecSkewFactor);
  m_cEncLib.setMaiSEISignFocalLengthX(m_maiSEISignFocalLengthX);
  m_cEncLib.setMaiSEIExponentFocalLengthX(m_maiSEIExponentFocalLengthX);
  m_cEncLib.setMaiSEIMantissaFocalLengthX(m_maiSEIMantissaFocalLengthX);
  m_cEncLib.setMaiSEISignFocalLengthY(m_maiSEISignFocalLengthY);
  m_cEncLib.setMaiSEIExponentFocalLengthY(m_maiSEIExponentFocalLengthY);
  m_cEncLib.setMaiSEIMantissaFocalLengthY(m_maiSEIMantissaFocalLengthY);
  m_cEncLib.setMaiSEISignPrincipalPointX(m_maiSEISignPrincipalPointX);
  m_cEncLib.setMaiSEIExponentPrincipalPointX(m_maiSEIExponentPrincipalPointX);
  m_cEncLib.setMaiSEIMantissaPrincipalPointX(m_maiSEIMantissaPrincipalPointX);
  m_cEncLib.setMaiSEISignPrincipalPointY(m_maiSEISignPrincipalPointY);
  m_cEncLib.setMaiSEIExponentPrincipalPointY(m_maiSEIExponentPrincipalPointY);
  m_cEncLib.setMaiSEIMantissaPrincipalPointY(m_maiSEIMantissaPrincipalPointY);
  m_cEncLib.setMaiSEISignSkewFactor(m_maiSEISignSkewFactor);
  m_cEncLib.setMaiSEIExponentSkewFactor(m_maiSEIExponentSkewFactor);
  m_cEncLib.setMaiSEIMantissaSkewFactor(m_maiSEIMantissaSkewFactor);
  m_cEncLib.setMaiSEIPrecRotationParam(m_maiSEIPrecRotationParam);
  m_cEncLib.setMaiSEIPrecTranslationParam(m_maiSEIPrecTranslationParam);
  m_cEncLib.setMvpSEIEnabled(m_mvpSEIEnabled);
  m_cEncLib.setMvpSEINumViewsMinus1(m_mvpSEINumViewsMinus1);
  m_cEncLib.setMvpSEIViewPosition(m_mvpSEIViewPosition);
  // alpha channel information sei
  m_cEncLib.setAciSEIEnabled(m_aciSEIEnabled);
  m_cEncLib.setAciSEICancelFlag(m_aciSEICancelFlag);
  m_cEncLib.setAciSEIUseIdc(m_aciSEIUseIdc);
  m_cEncLib.setAciSEIBitDepthMinus8(m_aciSEIBitDepthMinus8);
  m_cEncLib.setAciSEITransparentValue(m_aciSEITransparentValue);
  m_cEncLib.setAciSEIOpaqueValue(m_aciSEIOpaqueValue);
  m_cEncLib.setAciSEIIncrFlag(m_aciSEIIncrFlag);
  m_cEncLib.setAciSEIClipFlag(m_aciSEIClipFlag);
  m_cEncLib.setAciSEIClipTypeFlag(m_aciSEIClipTypeFlag);
  // depth representation information sei
  m_cEncLib.setDriSEIEnabled(m_driSEIEnabled);
  m_cEncLib.setDriSEIZNearFlag(m_driSEIZNearFlag);
  m_cEncLib.setDriSEIZFarFlag(m_driSEIZFarFlag);
  m_cEncLib.setDriSEIDMinFlag(m_driSEIDMinFlag);
  m_cEncLib.setDriSEIDMaxFlag(m_driSEIDMaxFlag);
  m_cEncLib.setDriSEIZNear(m_driSEIZNear);
  m_cEncLib.setDriSEIZFar(m_driSEIZFar);
  m_cEncLib.setDriSEIDMin(m_driSEIDMin);
  m_cEncLib.setDriSEIDMax(m_driSEIDMax);
  m_cEncLib.setDriSEIDepthRepresentationType(m_driSEIDepthRepresentationType);
  m_cEncLib.setDriSEIDisparityRefViewId(m_driSEIDisparityRefViewId);
  m_cEncLib.setDriSEINonlinearNumMinus1(m_driSEINonlinearNumMinus1);
  m_cEncLib.setDriSEINonlinearModel(m_driSEINonlinearModel);
#  if JVET_Z0120_SII_SEI_PROCESSING
  m_cEncLib.setShutterFilterFlag(m_ShutterFilterEnable);
  m_cEncLib.setBlendingRatioSII(m_SII_BlendingRatio);
#  endif
  m_cEncLib.setNNPostFilterSEICharacteristicsEnabled(
    m_nnPostFilterSEICharacteristicsEnabled);
  m_cEncLib.setNNPostFilterSEICharacteristicsNumFilters(
    m_nnPostFilterSEICharacteristicsNumFilters);
  for (int i = 0; i < m_nnPostFilterSEICharacteristicsNumFilters; i++) {
    m_cEncLib.setNNPostFilterSEICharacteristicsId(
      m_nnPostFilterSEICharacteristicsId[i], i);
#  if JVET_AD0056_MOVE_NNPFC_BASE_FLAG
    m_cEncLib.setNNPostFilterSEICharacteristicsBaseFlag(
      m_nnPostFilterSEICharacteristicsBaseFlag[i], i);
#  endif
    m_cEncLib.setNNPostFilterSEICharacteristicsModeIdc(
      m_nnPostFilterSEICharacteristicsModeIdc[i], i);
    m_cEncLib.setNNPostFilterSEICharacteristicsPropertyPresentFlag(
      m_nnPostFilterSEICharacteristicsPropertyPresentFlag[i], i);
    if (m_cEncLib.getNNPostFilterSEICharacteristicsPropertyPresentFlag(i)) {
#  if !JVET_AD0056_MOVE_NNPFC_BASE_FLAG
      m_cEncLib.setNNPostFilterSEICharacteristicsBaseFlag(
        m_nnPostFilterSEICharacteristicsBaseFlag[i], i);
#  endif
      if (!m_nnPostFilterSEICharacteristicsBaseFlag[i]) {
        bool baseFilterExist = false;
        for (int j = i - 1; j >= 0; j--) {
          if (m_cEncLib.getNNPostFilterSEICharacteristicsId(i)
              == m_cEncLib.getNNPostFilterSEICharacteristicsId(j)) {
            baseFilterExist = true;
            break;
          }
        }
        CHECK(!baseFilterExist,
              "No base filter found! Cannot have an update filter without "
              "base filter.")
      }
      m_cEncLib.setNNPostFilterSEICharacteristicsPurpose(
        m_nnPostFilterSEICharacteristicsPurpose[i], i);
      if ((m_cEncLib.getNNPostFilterSEICharacteristicsPurpose(i)
           & NNPC_PurposeType::CHROMA_UPSAMPLING)
          != 0) {
        m_cEncLib.setNNPostFilterSEICharacteristicsOutSubCFlag(
          m_nnPostFilterSEICharacteristicsOutSubCFlag[i], i);
      }
      if ((m_cEncLib.getNNPostFilterSEICharacteristicsPurpose(i)
           & NNPC_PurposeType::COLOURIZATION)
          != 0) {
        m_cEncLib.setNNPostFilterSEICharacteristicsOutColourFormatIdc(
          ChromaFormat(m_nnPostFilterSEICharacteristicsOutColourFormatIdc[i]),
          i);
      }
      if ((m_cEncLib.getNNPostFilterSEICharacteristicsPurpose(i)
           & NNPC_PurposeType::RESOLUTION_UPSAMPLING)
          != 0) {
        m_cEncLib.setNNPostFilterSEICharacteristicsPicWidthNumeratorMinus1(
          m_nnPostFilterSEICharacteristicsPicWidthNumerator[i] - 1, i);
        m_cEncLib.setNNPostFilterSEICharacteristicsPicWidthDenominatorMinus1(
          m_nnPostFilterSEICharacteristicsPicWidthDenominator[i] - 1, i);
        m_cEncLib.setNNPostFilterSEICharacteristicsPicHeightNumeratorMinus1(
          m_nnPostFilterSEICharacteristicsPicHeightNumerator[i] - 1, i);
        m_cEncLib.setNNPostFilterSEICharacteristicsPicHeightDenominatorMinus1(
          m_nnPostFilterSEICharacteristicsPicHeightDenominator[i] - 1, i);
      }

#  if JVET_AD0056_NNPFC_INPUT_PIC_OUTPUT_FLAG
      m_cEncLib
        .setNNPostFilterSEICharacteristicsNumberInputDecodedPicturesMinus1(
          m_nnPostFilterSEICharacteristicsNumberInputDecodedPicturesMinus1[i],
          i);
      if (m_nnPostFilterSEICharacteristicsNumberInputDecodedPicturesMinus1[i]
          > 0) {
        m_cEncLib.setNNPostFilterSEICharacteristicsInputPicOutputFlag(
          m_nnPostFilterSEICharacteristicsInputPicOutputFlag[i], i);
      }
#  endif
      if ((m_cEncLib.getNNPostFilterSEICharacteristicsPurpose(i)
           & NNPC_PurposeType::FRAME_RATE_UPSAMPLING)
          != 0) {
#  if !JVET_AD0056_NNPFC_INPUT_PIC_OUTPUT_FLAG
        m_cEncLib
          .setNNPostFilterSEICharacteristicsNumberInputDecodedPicturesMinus1(
            m_nnPostFilterSEICharacteristicsNumberInputDecodedPicturesMinus1
              [i],
            i);
#  endif
        m_cEncLib.setNNPostFilterSEICharacteristicsNumberInterpolatedPictures(
          m_nnPostFilterSEICharacteristicsNumberInterpolatedPictures[i], i);
#  if !JVET_AD0056_NNPFC_INPUT_PIC_OUTPUT_FLAG
        m_cEncLib.setNNPostFilterSEICharacteristicsInputPicOutputFlag(
          m_nnPostFilterSEICharacteristicsInputPicOutputFlag[i], i);
#  endif
      }
#  if JVET_AD0054_NNPFC_ABSENT_INPUT_PIC_ZERO_FLAG
      m_cEncLib.setNNPostFilterSEICharacteristicsAbsentInputPicZeroFlag(
        m_nnPostFilterSEICharacteristicsAbsentInputPicZeroFlag[i], i);
#  endif
      m_cEncLib.setNNPostFilterSEICharacteristicsComponentLastFlag(
        m_nnPostFilterSEICharacteristicsComponentLastFlag[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsInpFormatIdc(
        m_nnPostFilterSEICharacteristicsInpFormatIdc[i], i);
      if (m_cEncLib.getNNPostFilterSEICharacteristicsInpFormatIdc(i) == 1) {
        m_cEncLib.setNNPostFilterSEICharacteristicsInpTensorBitDepthLumaMinus8(
          m_nnPostFilterSEICharacteristicsInpTensorBitDepthLumaMinus8[i], i);
        m_cEncLib
          .setNNPostFilterSEICharacteristicsInpTensorBitDepthChromaMinus8(
            m_nnPostFilterSEICharacteristicsInpTensorBitDepthChromaMinus8[i],
            i);
      }
      m_cEncLib.setNNPostFilterSEICharacteristicsInpOrderIdc(
        m_nnPostFilterSEICharacteristicsInpOrderIdc[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsOutFormatIdc(
        m_nnPostFilterSEICharacteristicsOutFormatIdc[i], i);
      if (m_cEncLib.getNNPostFilterSEICharacteristicsOutFormatIdc(i) == 1) {
        m_cEncLib.setNNPostFilterSEICharacteristicsOutTensorBitDepthLumaMinus8(
          m_nnPostFilterSEICharacteristicsOutTensorBitDepthLumaMinus8[i], i);
        m_cEncLib
          .setNNPostFilterSEICharacteristicsOutTensorBitDepthChromaMinus8(
            m_nnPostFilterSEICharacteristicsOutTensorBitDepthChromaMinus8[i],
            i);
      }
      m_cEncLib.setNNPostFilterSEICharacteristicsOutOrderIdc(
        m_nnPostFilterSEICharacteristicsOutOrderIdc[i], i);
#  if JVET_AD0233_NNPFC_CHROMA_SAMPLE_LOC
      m_cEncLib.setNNPostFilterSEICharacteristicsChromaLocInfoPresentFlag(
        m_nnPostFilterSEICharacteristicsChromaLocInfoPresentFlag[i], i);
      if (m_cEncLib.getNNPostFilterSEICharacteristicsChromaLocInfoPresentFlag(
            i)) {
        m_cEncLib.setNNPostFilterSEICharacteristicsChromaSampleLocTypeFrame(
          static_cast<Chroma420LocType>(
            m_nnPostFilterSEICharacteristicsChromaSampleLocTypeFrame[i]),
          i);
      }
#  endif
      m_cEncLib.setNNPostFilterSEICharacteristicsConstantPatchSizeFlag(
        m_nnPostFilterSEICharacteristicsConstantPatchSizeFlag[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsPatchWidthMinus1(
        m_nnPostFilterSEICharacteristicsPatchWidthMinus1[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsPatchHeightMinus1(
        m_nnPostFilterSEICharacteristicsPatchHeightMinus1[i], i);
      if (m_nnPostFilterSEICharacteristicsConstantPatchSizeFlag[i] == 0) {
        m_cEncLib
          .setNNPostFilterSEICharacteristicsExtendedPatchWidthCdDeltaMinus1(
            m_nnPostFilterSEICharacteristicsExtendedPatchWidthCdDeltaMinus1[i],
            i);
        m_cEncLib
          .setNNPostFilterSEICharacteristicsExtendedPatchHeightCdDeltaMinus1(
            m_nnPostFilterSEICharacteristicsExtendedPatchHeightCdDeltaMinus1
              [i],
            i);
      }
      m_cEncLib.setNNPostFilterSEICharacteristicsOverlap(
        m_nnPostFilterSEICharacteristicsOverlap[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsPaddingType(
        m_nnPostFilterSEICharacteristicsPaddingType[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsLumaPadding(
        m_nnPostFilterSEICharacteristicsLumaPadding[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsCrPadding(
        m_nnPostFilterSEICharacteristicsCrPadding[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsCbPadding(
        m_nnPostFilterSEICharacteristicsCbPadding[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsComplexityInfoPresentFlag(
        m_nnPostFilterSEICharacteristicsComplexityInfoPresentFlag[i], i);
      if (m_cEncLib.getNNPostFilterSEICharacteristicsComplexityInfoPresentFlag(
            i)) {
        m_cEncLib.setNNPostFilterSEICharacteristicsLumaPadding(
          m_nnPostFilterSEICharacteristicsLumaPadding[i], i);
        m_cEncLib.setNNPostFilterSEICharacteristicsCrPadding(
          m_nnPostFilterSEICharacteristicsCrPadding[i], i);
        m_cEncLib.setNNPostFilterSEICharacteristicsCbPadding(
          m_nnPostFilterSEICharacteristicsCbPadding[i], i);
        m_cEncLib.setNNPostFilterSEICharacteristicsParameterTypeIdc(
          m_nnPostFilterSEICharacteristicsParameterTypeIdc[i], i);
        m_cEncLib
          .setNNPostFilterSEICharacteristicsLog2ParameterBitLengthMinus3(
            m_nnPostFilterSEICharacteristicsLog2ParameterBitLengthMinus3[i],
            i);
        m_cEncLib.setNNPostFilterSEICharacteristicsNumParametersIdc(
          m_nnPostFilterSEICharacteristicsNumParametersIdc[i], i);
        m_cEncLib.setNNPostFilterSEICharacteristicsNumKmacOperationsIdc(
          m_nnPostFilterSEICharacteristicsNumKmacOperationsIdc[i], i);
        m_cEncLib.setNNPostFilterSEICharacteristicsTotalKilobyteSize(
          m_nnPostFilterSEICharacteristicsTotalKilobyteSize[i], i);
      }
      m_cEncLib.setNNPostFilterSEICharacteristicsUriTag(
        m_nnPostFilterSEICharacteristicsUriTag[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsUri(
        m_nnPostFilterSEICharacteristicsUri[i], i);
    }
    if (m_cEncLib.getNNPostFilterSEICharacteristicsModeIdc(i)
        == POST_FILTER_MODE::ISO_IEC_15938_17) {
      m_cEncLib.setNNPostFilterSEICharacteristicsPayloadFilename(
        m_nnPostFilterSEICharacteristicsPayloadFilename[i], i);
    }
    m_cEncLib.setNNPostFilterSEICharacteristicsAuxInpIdc(
      m_nnPostFilterSEICharacteristicsAuxInpIdc[i], i);
    m_cEncLib.setNNPostFilterSEICharacteristicsSepColDescriptionFlag(
      m_nnPostFilterSEICharacteristicsSepColDescriptionFlag[i], i);
    if (m_cEncLib.getNNPostFilterSEICharacteristicsSepColDescriptionFlag(i)) {
      m_cEncLib.setNNPostFilterSEICharacteristicsColPrimaries(
        m_nnPostFilterSEICharacteristicsColPrimaries[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsTransCharacteristics(
        m_nnPostFilterSEICharacteristicsTransCharacteristics[i], i);
      m_cEncLib.setNNPostFilterSEICharacteristicsMatrixCoeffs(
        m_nnPostFilterSEICharacteristicsMatrixCoeffs[i], i);
    }
#  if JVET_AD0067_INCLUDE_SYNTAX
    if (m_cEncLib.getNNPostFilterSEICharacteristicsSepColDescriptionFlag(i)
        && (m_cEncLib.getNNPostFilterSEICharacteristicsOutFormatIdc(i) == 1)) {
      m_cEncLib.setNNPostFilterSEICharacteristicsFullRangeFlag(
        m_nnPostFilterSEICharacteristicsFullRangeFlag[i], i);
    }
#  endif
  }
  m_cEncLib.setNnPostFilterSEIActivationEnabled(
    m_nnPostFilterSEIActivationEnabled);
  m_cEncLib.setNnPostFilterSEIActivationTargetId(
    m_nnPostFilterSEIActivationTargetId);
#  if JVET_AD0388_NNPFA_OUTPUT_FLAG
  m_cEncLib.setNnPostFilterSEIActivationCancelFlag(
    m_nnPostFilterSEIActivationCancelFlag);
  m_cEncLib.setNnPostFilterSEIActivationTargetBaseFlag(
    m_nnPostFilterSEIActivationTargetBaseFlag);
  m_cEncLib.setNnPostFilterSEIActivationPersistenceFlag(
    m_nnPostFilterSEIActivationPersistenceFlag);
  m_cEncLib.setNnPostFilterSEIActivationOutputFlag(
    m_nnPostFilterSEIActivationOutputFlag);
#  endif
  m_cEncLib.setEntropyCodingSyncEnabledFlag(m_entropyCodingSyncEnabledFlag);
  m_cEncLib.setEntryPointPresentFlag(m_entryPointPresentFlag);
  m_cEncLib.setTMVPModeId(m_TMVPModeId);
  m_cEncLib.setSliceLevelRpl(m_sliceLevelRpl);
  m_cEncLib.setSliceLevelDblk(m_sliceLevelDblk);
  m_cEncLib.setSliceLevelSao(m_sliceLevelSao);
  m_cEncLib.setSliceLevelWp(m_sliceLevelWp);
  m_cEncLib.setSliceLevelDeltaQp(m_sliceLevelDeltaQp);
  m_cEncLib.setSliceLevelAlf(m_sliceLevelAlf);
  m_cEncLib.setUseScalingListId(m_useScalingListId);
  m_cEncLib.setScalingListFileName(m_scalingListFileName);
  m_cEncLib.setDisableScalingMatrixForLfnstBlks(
    m_disableScalingMatrixForLfnstBlks);
  m_cEncLib.setDisableScalingMatrixForAlternativeColourSpace(
    m_disableScalingMatrixForAlternativeColourSpace);

  if (m_cEncLib.getDisableScalingMatrixForAlternativeColourSpace()) {
    m_cEncLib.setScalingMatrixDesignatedColourSpace(
      m_scalingMatrixDesignatedColourSpace);
  }
  m_cEncLib.setConstrainedRaslencoding(m_constrainedRaslEncoding);
  m_cEncLib.setCraAPSreset(m_craAPSreset);
  m_cEncLib.setRprRASLtoolSwitch(m_rprRASLtoolSwitch);
  m_cEncLib.setDepQuantEnabledFlag(m_depQuantEnabledFlag);
  m_cEncLib.setSignDataHidingEnabledFlag(m_signDataHidingEnabledFlag);
  m_cEncLib.setUseRateCtrl(m_rcEnableRateControl);
  if (m_rcEnableRateControl) {
    m_cEncLib.setTargetBitrate(m_rcTargetBitrate);
    m_cEncLib.setKeepHierBit(m_rcKeepHierarchicalBit);
    m_cEncLib.setLCULevelRC(m_rcCtuLevelRateControl);
    m_cEncLib.setUseLCUSeparateModel(m_rcUseCtuSeparateModel);
    m_cEncLib.setInitialQP(m_rcInitialQp);
    m_cEncLib.setForceIntraQP(m_rcForceIntraQp);
    m_cEncLib.setCpbSaturationEnabled(m_rcCpbSaturationEnabled);
    m_cEncLib.setCpbSize(m_rcCpbSize);
    m_cEncLib.setInitialCpbFullness(m_rcInitialCpbFullness);
  }
  m_cEncLib.setCostMode(m_costMode);
  m_cEncLib.setTSRCdisableLL(m_TSRCdisableLL);
  m_cEncLib.setUseRecalculateQPAccordingToLambda(
    m_recalculateQPAccordingToLambda);
  m_cEncLib.setDCIEnabled(m_DCIEnabled);
  m_cEncLib.setSiiSEIEnabled(m_siiSEIEnabled);
  m_cEncLib.setSiiSEINumUnitsInShutterInterval(
    m_siiSEINumUnitsInShutterInterval);
  m_cEncLib.setSiiSEITimeScale(m_siiSEITimeScale);
  m_cEncLib.setSiiSEISubLayerNumUnitsInSI(m_siiSEISubLayerNumUnitsInSI);

  m_cEncLib.setPoSEIEnabled(m_poSEIEnabled);
#  if JVET_AD0386_SEI
  m_cEncLib.setPoSEIPrefixFlag(m_poSEIPrefixFlag);
#  endif
  m_cEncLib.setPoSEIPayloadType(m_poSEIPayloadType);
  m_cEncLib.setPoSEIProcessingOrder(m_poSEIProcessingOrder);
  m_cEncLib.setPoSEIPrefixByte(m_poSEIPrefixByte);

  m_cEncLib.setPostFilterHintSEIEnabled(m_postFilterHintSEIEnabled);
  m_cEncLib.setPostFilterHintSEICancelFlag(m_postFilterHintSEICancelFlag);
  m_cEncLib.setPostFilterHintSEIPersistenceFlag(
    m_postFilterHintSEIPersistenceFlag);
  m_cEncLib.setPostFilterHintSEISizeY(m_postFilterHintSEISizeY);
  m_cEncLib.setPostFilterHintSEISizeX(m_postFilterHintSEISizeX);
  m_cEncLib.setPostFilterHintSEIType(m_postFilterHintSEIType);
  m_cEncLib.setPostFilterHintSEIChromaCoeffPresentFlag(
    m_postFilterHintSEIChromaCoeffPresentFlag);
  m_cEncLib.setPostFilterHintSEIValues(m_postFilterHintValues);

  m_cEncLib.setVuiParametersPresentFlag(m_vuiParametersPresentFlag);
  m_cEncLib.setSamePicTimingInAllOLS(m_samePicTimingInAllOLS);
  m_cEncLib.setAspectRatioInfoPresentFlag(m_aspectRatioInfoPresentFlag);
  m_cEncLib.setAspectRatioIdc(m_aspectRatioIdc);
  m_cEncLib.setSarWidth(m_sarWidth);
  m_cEncLib.setSarHeight(m_sarHeight);
  m_cEncLib.setColourDescriptionPresentFlag(m_colourDescriptionPresentFlag);
  m_cEncLib.setColourPrimaries(m_colourPrimaries);
  m_cEncLib.setTransferCharacteristics(m_transferCharacteristics);
  m_cEncLib.setMatrixCoefficients(m_matrixCoefficients);
  m_cEncLib.setProgressiveSourceFlag(m_progressiveSourceFlag);
  m_cEncLib.setInterlacedSourceFlag(m_interlacedSourceFlag);
  m_cEncLib.setChromaLocInfoPresentFlag(m_chromaLocInfoPresentFlag);
  m_cEncLib.setChromaSampleLocTypeTopField(m_chromaSampleLocTypeTopField);
  m_cEncLib.setChromaSampleLocTypeBottomField(
    m_chromaSampleLocTypeBottomField);
  m_cEncLib.setChromaSampleLocType(m_chromaSampleLocType);
  m_cEncLib.setOverscanInfoPresentFlag(m_overscanInfoPresentFlag);
  m_cEncLib.setOverscanAppropriateFlag(m_overscanAppropriateFlag);
  m_cEncLib.setVideoFullRangeFlag(m_videoFullRangeFlag);
  m_cEncLib.setFieldSeqFlag(m_isField);
  m_cEncLib.setEfficientFieldIRAPEnabled(m_efficientFieldIRAPEnabled);
  m_cEncLib.setHarmonizeGopFirstFieldCoupleEnabled(
    m_harmonizeGopFirstFieldCoupleEnabled);
  m_cEncLib.setSummaryOutFilename(m_summaryOutFilename);
  m_cEncLib.setSummaryPicFilenameBase(m_summaryPicFilenameBase);
  m_cEncLib.setSummaryVerboseness(m_summaryVerboseness);
  m_cEncLib.setIMV(m_ImvMode);
  m_cEncLib.setIMV4PelFast(m_Imv4PelFast);
  m_cEncLib.setDecodeBitstream(0, m_decodeBitstreams[0]);
  m_cEncLib.setDecodeBitstream(1, m_decodeBitstreams[1]);
  m_cEncLib.setSwitchPOC(m_switchPOC);
  m_cEncLib.setSwitchDQP(m_switchDQP);
  m_cEncLib.setFastForwardToPOC(m_fastForwardToPOC);
  m_cEncLib.setForceDecodeBitstream1(m_forceDecodeBitstream1);
  m_cEncLib.setStopAfterFFtoPOC(m_stopAfterFFtoPOC);
  m_cEncLib.setBs2ModPOCAndType(m_bs2ModPOCAndType);
  m_cEncLib.setDebugCTU(m_debugCTU);
  m_cEncLib.setUseALF(m_alf);
  m_cEncLib.setAlfTrueOrg(m_alfTrueOrg);
  m_cEncLib.setALFStrengthLuma(m_alfStrengthLuma);
  m_cEncLib.setCCALFStrength(m_ccalfStrength);
  m_cEncLib.setALFAllowPredefinedFilters(m_alfAllowPredefinedFilters);
  m_cEncLib.setALFStrengthChroma(m_alfStrengthChroma);
  m_cEncLib.setALFStrengthTargetLuma(m_alfStrengthTargetLuma);
  m_cEncLib.setALFStrengthTargetChroma(m_alfStrengthTargetChroma);
  m_cEncLib.setCCALFStrengthTarget(m_ccalfStrengthTarget);
  m_cEncLib.setUseCCALF(m_ccalf);
  m_cEncLib.setCCALFQpThreshold(m_ccalfQpThreshold);
  m_cEncLib.setGOPBasedRPRQPThreshold(m_gopBasedRPRQPThreshold);
  m_cEncLib.setLmcs(m_lmcsEnabled);
  m_cEncLib.setReshapeSignalType(m_reshapeSignalType);
  m_cEncLib.setReshapeIntraCMD(m_intraCMD);
  m_cEncLib.setReshapeCW(m_reshapeCW);
  m_cEncLib.setReshapeCSoffset(m_CSoffset);
  m_cEncLib.setMaxNumALFAPS(m_maxNumAlfAps);
  m_cEncLib.setALFAPSIDShift(m_alfapsIDShift);
  m_cEncLib.setConstantJointCbCrSignFlag(m_constantJointCbCrSignFlag != 0);
#  if JVET_O0756_CALCULATE_HDRMETRICS
  for (int i = 0; i < hdrtoolslib::NB_REF_WHITE; i++) {
    m_cEncLib.setWhitePointDeltaE(i, m_whitePointDeltaE[i]);
  }
  m_cEncLib.setMaxSampleValue(m_maxSampleValue);
  m_cEncLib.setSampleRange(m_sampleRange);
  m_cEncLib.setColorPrimaries(m_colorPrimaries);
  m_cEncLib.setEnableTFunctionLUT(m_enableTFunctionLUT);
  for (int i = 0; i < 2; i++) {
    m_cEncLib.setChromaLocation(i, m_chromaLocation);
    m_cEncLib.setChromaUPFilter(m_chromaUPFilter);
  }
  m_cEncLib.setCropOffsetLeft(m_cropOffsetLeft);
  m_cEncLib.setCropOffsetTop(m_cropOffsetTop);
  m_cEncLib.setCropOffsetRight(m_cropOffsetRight);
  m_cEncLib.setCropOffsetBottom(m_cropOffsetBottom);
  m_cEncLib.setCalculateHdrMetrics(m_calculateHdrMetrics);
#  endif

  m_cEncLib.setSEIManifestSEIEnabled(m_SEIManifestSEIEnabled);
  m_cEncLib.setSEIPrefixIndicationSEIEnabled(m_SEIPrefixIndicationSEIEnabled);

  m_cEncLib.setOPIEnabled(m_OPIEnabled);
  if (m_OPIEnabled) {
    if (m_maxTemporalLayer != 500) {
      m_cEncLib.setHtidPlus1(m_maxTemporalLayer + 1);
    }
    if (m_targetOlsIdx != 500) { m_cEncLib.setTargetOlsIdx(m_targetOlsIdx); }
  }
  m_cEncLib.setRplOfDepLayerInSh(m_rplOfDepLayerInSh);

  m_cEncLib.setGopBasedTemporalFilterEnabled(m_gopBasedTemporalFilterEnabled);
  m_cEncLib.setBIM(m_bimEnabled);
  m_cEncLib.setNumRefLayers(m_numRefLayers);

  m_cEncLib.setVPSParameters(m_cfgVPSParameters);
}

template<typename T>
void
vtmVideoEncoderImpl<T>::xInitLib() {
  m_cEncLib.init(this);
}

/**
  Write access units to output file.
  \param bitstreamFile  target bitstream file
  \param iNumEncoded    number of encoded frames
  \param accessUnits    list of access units to be written
 */

template<typename T>
void
vtmVideoEncoderImpl<T>::xWriteOutput(std::ostream&           bitstreamFile,
                                     int                     iNumEncoded,
                                     std::list<PelUnitBuf*>& recBufList,
                                     FrameSequence<T>&       videoRec) {
  const InputColourSpaceConversion ipCSC = (!m_outputInternalColourSpace)
                                             ? m_inputColourSpaceConvert
                                             : IPCOLOURSPACE_UNCHANGED;
  std::list<PelUnitBuf*>::iterator iterPicYuvRec = recBufList.end();
  int                              i;

  for (i = 0; i < iNumEncoded; i++) { --iterPicYuvRec; }

  if (m_isField) {
    // Reinterlace fields
    for (i = 0; i < iNumEncoded / 2; i++) {
      const PelUnitBuf* pcPicYuvRecTop = *(iterPicYuvRec++);
      iterPicYuvRec++;
      if (!m_reconFileName.empty()) {
        xWritePicture(pcPicYuvRecTop, videoRec);
      }
    }
  } else {
    for (i = 0; i < iNumEncoded; i++) {
      const PelUnitBuf* pcPicYuvRec = *(iterPicYuvRec++);
      if (!m_reconFileName.empty()) {
        if (m_cEncLib.isResChangeInClvsEnabled()
            && m_cEncLib.getUpscaledOutput()) {
          xWritePicture(pcPicYuvRec, videoRec);
        } else {
          xWritePicture(pcPicYuvRec, videoRec);
        }
      }
    }
  }
}

template<typename T>
void
vtmVideoEncoderImpl<T>::destroyLib() {
  printf("\nLayerId %2d", m_cEncLib.getLayerId());

  m_cEncLib.printSummary(m_isField);

  // delete used buffers in encoder class
  m_cEncLib.deletePicBuffer();

  for (auto& p : m_recBufList) { delete p; }
  m_recBufList.clear();

  xDestroyLib();

  m_orgPic->destroy();
  m_trueOrgPic->destroy();
  delete m_trueOrgPic;
  delete m_orgPic;

  if (m_sourceScalingRatioHor != 1.0 || m_sourceScalingRatioVer != 1.0) {
    m_orgPicBeforeScale->destroy();
    m_trueOrgPicBeforeScale->destroy();
    delete m_trueOrgPicBeforeScale;
    delete m_orgPicBeforeScale;
  }

  if (m_resChangeInClvsEnabled && m_gopBasedRPREnabledFlag) {
    for (int i = 0; i < 2; i++) {
      m_rprPic[i]->destroy();
      delete m_rprPic[i];
    }
  }
  if (m_gopBasedTemporalFilterEnabled || m_bimEnabled) {
    m_filteredOrgPic->destroy();
    delete m_filteredOrgPic;
  }
  if (m_bimEnabled) {
    auto map = m_cEncLib.getAdaptQPmap();
    for (auto it = map->begin(); it != map->end(); ++it) {
      int* p = it->second;
      delete p;
    }
  }
  if (m_fgcSEIAnalysisEnabled && m_fgcSEIExternalDenoised.empty()) {
    m_filteredOrgPicForFG->destroy();
    delete m_filteredOrgPicForFG;
    m_filteredOrgPicForFG = nullptr;
  }
#  if EXTENSION_360_VIDEO
  delete m_ext360;
#  endif

  printRateSummary();
}

template<typename T>
void
vtmVideoEncoderImpl<T>::printRateSummary() {
  double time = (double)m_iFrameRcvd / m_frameRate.getFloatVal()
                * m_temporalSubsampleRatio;
  msg(VTM_DETAILS,
      "Bytes written to file: %u (%.3f kbps)\n",
      m_totalBytes,
      0.008 * m_totalBytes / time);
  if (m_summaryVerboseness > 0) {
    msg(VTM_DETAILS,
        "Bytes for SPS/PPS/APS/Slice (Incl. Annex B): %u (%.3f kbps)\n",
        m_essentialBytes,
        0.008 * m_essentialBytes / time);
  }
}

template<typename T>
void
vtmVideoEncoderImpl<T>::xDestroyLib() {
  // Neo Decoder
  m_cEncLib.destroy();
}

template<typename T>
void
vtmVideoEncoderImpl<T>::printChromaFormat() {
  if (g_verbosity >= VTM_DETAILS) {
    std::cout << std::setw(43) << "Input ChromaFormatIDC = ";
    switch (m_inputChromaFormatIDC) {
    case ChromaFormat::_400: std::cout << "  4:0:0"; break;
    case ChromaFormat::_420: std::cout << "  4:2:0"; break;
    case ChromaFormat::_422: std::cout << "  4:2:2"; break;
    case ChromaFormat::_444: std::cout << "  4:4:4"; break;
    default: THROW("invalid chroma fomat");
    }
    std::cout << std::endl;

    std::cout << std::setw(43) << "Output (internal) ChromaFormatIDC = ";
    switch (m_cEncLib.getChromaFormatIdc()) {
    case ChromaFormat::_400: std::cout << "  4:0:0"; break;
    case ChromaFormat::_420: std::cout << "  4:2:0"; break;
    case ChromaFormat::_422: std::cout << "  4:2:2"; break;
    case ChromaFormat::_444: std::cout << "  4:4:4"; break;
    default: THROW("invalid chroma fomat");
    }
    std::cout << "\n" << std::endl;
  }
}

template<typename T>
void
vtmVideoEncoderImpl<T>::xWritePicture(const PelUnitBuf* pic,
                                      FrameSequence<T>& video) {
  int chromaSubsample =
    pic->chromaFormat == ChromaFormat::_400
      ? std::numeric_limits<int>::max()
      : pic->get(COMPONENT_Y).width / pic->get(COMPONENT_Cb).width;
  int         width  = m_sourceWidth - m_confWinLeft - m_confWinRight;
  int         height = m_sourceHeight - m_confWinTop - m_confWinBottom;
  ColourSpace format = m_cEncLib.getChromaFormatIdc() == ChromaFormat::_420
                         ? ColourSpace::YUV420p
                       : m_cEncLib.getChromaFormatIdc() == ChromaFormat::_400
                         ? ColourSpace::YUV400p
                       : m_rgbFormat ? ColourSpace::RGB444p
                                     : ColourSpace::YUV444p;
  video.resize(width, height, format, video.frameCount() + 1);

  auto& image = video.frame(video.frameCount() - 1);
  image.set(pic->get(COMPONENT_Y).bufAt(0, 0),
            pic->get(COMPONENT_Cb).bufAt(0, 0),
            pic->get(COMPONENT_Cr).bufAt(0, 0),
            width,
            height,
            pic->get(COMPONENT_Y).stride,
            width / chromaSubsample,
            height / chromaSubsample,
            pic->get(COMPONENT_Cb).stride,
            m_internalBitDepth[ChannelType::LUMA]
              - m_outputBitDepth[ChannelType::LUMA],
            format,
            m_inputColourSpaceConvert == IPCOLOURSPACE_RGBtoGBR);
}

template<typename T>
void
vtmVideoEncoderImpl<T>::xReadPicture(PelUnitBuf*       pic,
                                     FrameSequence<T>& video,
                                     int               frameIndex) {
  auto& image = video.frame(frameIndex);
  image.get(pic->get(COMPONENT_Y).bufAt(0, 0),
            pic->get(COMPONENT_Cb).bufAt(0, 0),
            pic->get(COMPONENT_Cr).bufAt(0, 0),
            pic->get(COMPONENT_Y).width,
            pic->get(COMPONENT_Y).height,
            pic->get(COMPONENT_Y).stride,
            pic->get(COMPONENT_Cb).width,
            pic->get(COMPONENT_Cb).height,
            pic->get(COMPONENT_Cb).stride,
            m_internalBitDepth[ChannelType::LUMA]
              - m_outputBitDepth[ChannelType::LUMA],
            m_inputColourSpaceConvert == IPCOLOURSPACE_RGBtoGBR);
}

template class vtmVideoEncoderImpl<uint8_t>;
template class vtmVideoEncoderImpl<uint16_t>;

}  // namespace vmesh

#endif
