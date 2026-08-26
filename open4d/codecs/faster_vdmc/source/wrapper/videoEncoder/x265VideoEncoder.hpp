/* BSD-2-Clause */
#pragma once

#if defined(USE_X265_VIDEO_CODEC)

#include "videoEncoder.hpp"

namespace vmesh {

template<class T>
class x265VideoEncoder : public VideoEncoder<T> {
public:
  void encode(FrameSequence<T>&       videoSrc,
              VideoEncoderParameters& params,
              std::vector<uint8_t>&   bitstream,
              FrameSequence<T>&       videoRec) override;
};

}  // namespace vmesh

#endif
