/* BSD-2-Clause */
#if defined(USE_X265_VIDEO_CODEC)

#include "x265VideoEncoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include <x265.h>

namespace vmesh {
namespace {

int
toX265ColourSpace(ColourSpace colourSpace) {
  switch (colourSpace) {
  case ColourSpace::YUV400p: return X265_CSP_I400;
  case ColourSpace::YUV420p: return X265_CSP_I420;
  case ColourSpace::YUV422p: return X265_CSP_I422;
  case ColourSpace::YUV444p:
  case ColourSpace::RGB444p:
  case ColourSpace::BGR444p:
  case ColourSpace::GBR444p: return X265_CSP_I444;
  default: throw std::runtime_error("x265: unsupported colour space");
  }
}

void
appendNals(std::vector<uint8_t>& bitstream, x265_nal* nals, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    bitstream.insert(
      bitstream.end(), nals[i].payload, nals[i].payload + nals[i].sizeBytes);
  }
}

template<typename T>
void
copyReconstruction(const x265_picture& picture, Frame<T>& destination) {
  for (int planeIndex = 0; planeIndex < destination.planeCount(); ++planeIndex) {
    auto& plane = destination.plane(planeIndex);
    const auto* source = static_cast<const uint8_t*>(picture.planes[planeIndex]);
    for (int y = 0; y < plane.height(); ++y) {
      std::memcpy(plane.data() + static_cast<size_t>(y) * plane.width(),
                  source + static_cast<size_t>(y) * picture.stride[planeIndex],
                  static_cast<size_t>(plane.width()) * sizeof(T));
    }
  }
}

}  // namespace

template<typename T>
void
x265VideoEncoder<T>::encode(FrameSequence<T>&       videoSrc,
                            VideoEncoderParameters& params,
                            std::vector<uint8_t>&   bitstream,
                            FrameSequence<T>&       videoRec) {
  if (videoSrc.frameCount() == 0) {
    bitstream.clear();
    videoRec.clear();
    return;
  }

  std::unique_ptr<x265_param, decltype(&x265_param_free)> encoderParams(
    x265_param_alloc(), x265_param_free);
  if (!encoderParams
      || x265_param_default_preset(
           encoderParams.get(), params.encoderPreset_.c_str(), nullptr)
           < 0) {
    throw std::runtime_error("x265: invalid encoder preset");
  }

  encoderParams->sourceWidth      = videoSrc.width();
  encoderParams->sourceHeight     = videoSrc.height();
  encoderParams->internalCsp      = toX265ColourSpace(videoSrc.colourSpace());
  encoderParams->internalBitDepth = params.internalBitDepth_;
  encoderParams->fpsNum           = 30;
  encoderParams->fpsDenom         = 1;
  encoderParams->totalFrames      = videoSrc.frameCount();
  encoderParams->bAnnexB          = 1;
  encoderParams->bRepeatHeaders   = 0;
  encoderParams->bframes          = 0;
  encoderParams->bOpenGOP         = 0;
  encoderParams->bEnableWavefront = params.enableWavefront_ ? 1 : 0;
  encoderParams->frameNumThreads  = params.frameThreadCount_;
  encoderParams->maxSlices        = std::max(1, params.sliceCount_);
  encoderParams->rc.rateControlMode = X265_RC_CQP;
  encoderParams->rc.qp              = params.qp_;
  encoderParams->bEnablePsnr        = 1;
  encoderParams->logLevel           = X265_LOG_INFO;
  if (params.poolThreadCount_ > 0) {
    std::snprintf(encoderParams->numaPools,
                  sizeof(encoderParams->numaPools),
                  "%d",
                  params.poolThreadCount_);
  }
  if (params.allIntra_) {
    encoderParams->keyframeMin = 1;
    encoderParams->keyframeMax = 1;
    encoderParams->scenecutThreshold = 0;
  } else {
    encoderParams->keyframeMin = videoSrc.frameCount();
    encoderParams->keyframeMax = videoSrc.frameCount();
    encoderParams->scenecutThreshold = 0;
  }

  std::unique_ptr<x265_encoder, decltype(&x265_encoder_close)> encoder(
    x265_encoder_open(encoderParams.get()), x265_encoder_close);
  if (!encoder) throw std::runtime_error("x265: could not open encoder");

  bitstream.clear();
  x265_nal* headers     = nullptr;
  uint32_t  headerCount = 0;
  if (x265_encoder_headers(encoder.get(), &headers, &headerCount) < 0)
    throw std::runtime_error("x265: could not generate headers");
  appendNals(bitstream, headers, headerCount);

  videoRec.resize(videoSrc.width(),
                  videoSrc.height(),
                  videoSrc.colourSpace(),
                  videoSrc.frameCount());

  x265_picture inputPicture;
  x265_picture outputPicture;
  x265_picture_init(encoderParams.get(), &inputPicture);
  x265_picture_init(encoderParams.get(), &outputPicture);

  auto consumeOutput = [&](int result, x265_nal* nals, uint32_t nalCount) {
    if (result < 0) throw std::runtime_error("x265: encoding failed");
    if (result == 0) return;
    appendNals(bitstream, nals, nalCount);
    if (outputPicture.poc < 0 || outputPicture.poc >= videoRec.frameCount())
      throw std::runtime_error("x265: invalid reconstructed picture order");
    copyReconstruction(outputPicture, videoRec.frame(outputPicture.poc));
  };

  for (int frameIndex = 0; frameIndex < videoSrc.frameCount(); ++frameIndex) {
    auto& frame = videoSrc.frame(frameIndex);
    x265_picture_init(encoderParams.get(), &inputPicture);
    inputPicture.pts        = frameIndex;
    inputPicture.bitDepth   = params.inputBitDepth_;
    inputPicture.colorSpace = encoderParams->internalCsp;
    for (int planeIndex = 0; planeIndex < frame.planeCount(); ++planeIndex) {
      inputPicture.planes[planeIndex] = frame.plane(planeIndex).data();
      inputPicture.stride[planeIndex] = frame.plane(planeIndex).width()
                                        * static_cast<int>(sizeof(T));
    }
    x265_nal* nals     = nullptr;
    uint32_t  nalCount = 0;
    const int result = x265_encoder_encode(
      encoder.get(), &nals, &nalCount, &inputPicture, &outputPicture);
    consumeOutput(result, nals, nalCount);
  }

  while (true) {
    x265_nal* nals     = nullptr;
    uint32_t  nalCount = 0;
    const int result = x265_encoder_encode(
      encoder.get(), &nals, &nalCount, nullptr, &outputPicture);
    if (result == 0) break;
    consumeOutput(result, nals, nalCount);
  }

  std::cout << "x265 encoded " << videoSrc.frameCount() << " frames into "
            << bitstream.size() << " bytes\n";
}

template class x265VideoEncoder<uint8_t>;
template class x265VideoEncoder<uint16_t>;

}  // namespace vmesh

#endif
