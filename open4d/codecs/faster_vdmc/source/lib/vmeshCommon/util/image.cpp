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

#include <sstream>

#include "util/image.hpp"

namespace vmesh {

#ifndef STB_IMAGE_IMPLEMENTATION
// #define STB_IMAGE_IMPLEMENTATION
#  include <stb_image.h>
#endif

#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
// #define STB_IMAGE_WRITE_IMPLEMENTATION
#  include <stb_image_write.h>
#endif

//============================================================================

template<typename T>
bool
Frame<T>::loadImage(const std::string& fileName) {
  int32_t                    width        = 0;
  int32_t                    height       = 0;
  int32_t                    channelCount = 0;
  std::unique_ptr<uint8_t[]> buffer(
    stbi_load(fileName.c_str(), &width, &height, &channelCount, 0));
  if (buffer == nullptr || channelCount != 3) {
    printf("Error loading file: %s \n", fileName.c_str());
    return false;
  }
  resize(width, height, ColourSpace::BGR444p);
  auto& B = plane(0);
  auto& G = plane(1);
  auto& R = plane(2);
  for (int32_t y = 0, i = 0; y < height; y++) {
    for (int32_t x = 0; x < width; x++) {
      const auto r = buffer[i + 0];
      const auto g = buffer[i + 1];
      const auto b = buffer[i + 2];
      R.set(y, x, r);
      G.set(y, x, g);
      B.set(y, x, b);
      i += 3;
    }
  }
  return true;
}

//============================================================================

template<typename T>
bool
Frame<T>::load(const std::string& fileName, const int32_t frameIndex) {
  return load(expandNum(fileName, frameIndex));
}

//============================================================================

template<typename T>
bool
Frame<T>::load(const std::string& fileName) {
  auto ext = extension(fileName);
  if (ext == "png" || ext == "jpg" || ext == "bmp" || ext == "tga") {
    return loadImage(fileName);
  } else if (ext == "yuv" || ext == "rgb" || ext == "bgr") {
    std::ifstream is(fileName, std::ios::binary);
    if (!is.is_open()) { return false; }
    load(is);
  } else {
    printf("Error: can't load image: %s \n", fileName.c_str());
    return false;
  }
  return true;
}

//============================================================================

template<typename T>
bool
Frame<T>::saveImage(const std::string& fileName,
                    const ImageFormat  format,
                    const int32_t      quality) const {
  if (!((_colourSpace == ColourSpace::BGR444p)
        || (_colourSpace == ColourSpace::RGB444p)
        || (_colourSpace == ColourSpace::GBR444p))) {
    printf("Save frame must be ColourSpace::BGR444p or ColourSpace::RGB444p "
           "or ColourSpace::GBR444p \n");
    exit(-1);
  }
  const auto           channelCount = planeCount();
  std::vector<uint8_t> buffer;
  buffer.resize(channelCount * _width * _height);
  const auto& B = (_colourSpace == ColourSpace::BGR444p)   ? plane(0)
                  : (_colourSpace == ColourSpace::RGB444p) ? plane(2)
                                                           : plane(1);
  const auto& G = (_colourSpace == ColourSpace::BGR444p)   ? plane(1)
                  : (_colourSpace == ColourSpace::RGB444p) ? plane(1)
                                                           : plane(0);
  const auto& R = (_colourSpace == ColourSpace::BGR444p)   ? plane(2)
                  : (_colourSpace == ColourSpace::RGB444p) ? plane(0)
                                                           : plane(2);
  for (int32_t y = 0, i = 0; y < _height; y++) {
    for (int32_t x = 0; x < _width; x++) {
      buffer[i + 0] = R.get(y, x);
      buffer[i + 1] = G.get(y, x);
      buffer[i + 2] = B.get(y, x);
      i += 3;
    }
  }
  bool ret = false;
  switch (format) {
  case ImageFormat::JPG:
    ret = (stbi_write_jpg(fileName.c_str(),
                          _width,
                          _height,
                          channelCount,
                          buffer.data(),
                          quality)
           != 0);
    break;
  case ImageFormat::TGA:
    ret = (stbi_write_tga(
             fileName.c_str(), _width, _height, channelCount, buffer.data())
           != 0);
    break;
  case ImageFormat::BMP:
    ret = (stbi_write_bmp(
             fileName.c_str(), _width, _height, channelCount, buffer.data())
           != 0);
    break;
  case ImageFormat::PNG:
  default:
    ret = (stbi_write_png(fileName.c_str(),
                          _width,
                          _height,
                          channelCount,
                          buffer.data(),
                          _width * channelCount)
           != 0);
    break;
  }
  return ret;
}

//============================================================================

template<typename T>
bool
Frame<T>::save(const std::string& fileName, const int32_t frameIndex) const {
  return save(expandNum(fileName, frameIndex));
}

//============================================================================

template<typename T>
bool
Frame<T>::save(const std::string& fileName) const {
  auto ext = extension(fileName);
  if (ext == "png") {
    return saveImage(fileName, ImageFormat::PNG);
  } else if (ext == "jpg") {
    return saveImage(fileName, ImageFormat::JPG);
  } else if (ext == "bmp") {
    return saveImage(fileName, ImageFormat::BMP);
  } else if (ext == "tga") {
    return saveImage(fileName, ImageFormat::TGA);
  } else if (ext == "yuv" || ext == "rgb" || ext == "bgr") {
    std::ofstream os(fileName, std::ios::binary);
    if (!os.is_open()) { return false; }
    save(os);
  } else {
    printf("Error: can't save image: %s \n", fileName.c_str());
    return false;
  }
  return true;
}

//============================================================================
template<typename T>
bool
Frame<T>::saveSequence(const std::string& fileName, bool firstFrame) const {
  std::ofstream os;
  if (firstFrame) os.open(fileName, std::ios::binary);
  else os.open(fileName, std::ios::binary | std::ios::app);
  if (!os.is_open()) { return false; }
  save(os);
  return true;
}

template<typename T>
bool
FrameSequence<T>::load(const std::string& path,
                       const int32_t      frameStart,
                       const int32_t      frameCount) {
  printf("Loading a sequence of images from %4d to %4d \n",
         frameStart,
         frameStart + frameCount - 1);
  resize(0, 0, ColourSpace::BGR444p, frameCount);
  auto frameIndex = frameStart;
  for (auto& frame : _frames) {
    const auto name = vmesh::expandNum(path, frameIndex);
    if (!frame.loadImage(name)) {
      printf("Error loading image %d: %s \n", frameIndex, name.c_str());
      return false;
    }
    frameIndex++;
  }
  return true;
}

//============================================================================

template<typename T>
bool
FrameSequence<T>::save(const std::string& path, const int32_t frameStart) {
  printf("Saving a sequence of images from %4d to %4zu \n",
         frameStart,
         frameStart + _frames.size() - 1);
  auto frameIndex = frameStart;
  for (auto& frame : _frames) {
    const auto name = vmesh::expandNum(path, frameIndex);
    if (!frame.saveImage(name)) {
      printf("Error saving image %d: %s \n", frameIndex, name.c_str());
      return false;
    }
    frameIndex++;
  }
  return true;
}

//============================================================================

template bool Frame<uint8_t>::loadImage(const std::string& path);
template bool Frame<uint8_t>::saveImage(const std::string& path,
                                        const ImageFormat  format,
                                        const int32_t      quality) const;

template bool Frame<uint8_t>::load(const std::string& path,
                                   const int32_t      frameIndex);
template bool Frame<uint8_t>::save(const std::string& path,
                                   const int32_t      frameIndex) const;

template bool Frame<uint8_t>::load(const std::string& path);
template bool Frame<uint8_t>::save(const std::string& path) const;

//============================================================================
template bool Frame<uint16_t>::saveSequence(const std::string& fileName,
                                            bool firstFrame) const;
template bool Frame<uint8_t>::saveSequence(const std::string& fileName,
                                           bool firstFrame) const;
//============================================================================

template bool FrameSequence<uint8_t>::load(const std::string& path,
                                           const int32_t      frameStart,
                                           const int32_t      frameCount);

template bool FrameSequence<uint8_t>::save(const std::string& path,
                                           const int32_t      frameStart);

//============================================================================
}  // namespace vmesh
