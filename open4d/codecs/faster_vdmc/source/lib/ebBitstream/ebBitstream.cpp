/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2023, ISO/IEC
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
#include "ebBitstream.h"
#include "MD5.h"

using namespace eb;

Bitstream::Bitstream() {
  position_.bytes_ = 0;
  position_.bits_  = 0;
  data_.clear();
#if defined(BITSTREAM_TRACE)
  trace_ = false;
#endif
}

Bitstream::~Bitstream() { data_.clear(); }

bool
Bitstream::initialize(const Bitstream& bitstream) {
  position_.bytes_ = 0;
  position_.bits_  = 0;
  data_.resize(bitstream.data_.size(), 0);
  memcpy(data_.data(), bitstream.data_.data(), bitstream.data_.size());
  return true;
}

bool
Bitstream::initialize(std::vector<uint8_t>& data) {
  position_.bytes_ = 0;
  position_.bits_  = 0;
  data_.resize(data.size(), 0);
  memcpy(data_.data(), data.data(), data.size());
  return true;
}

bool
Bitstream::load(const std::string& compressedStreamPath) {
  std::ifstream fin(compressedStreamPath, std::ios::binary);
  if (!fin.is_open()) { return false; }
  fin.seekg(0, std::ios::end);
  uint64_t bitStreamSize = fin.tellg();
  fin.seekg(0, std::ios::beg);
  initialize(bitStreamSize);
  fin.read(reinterpret_cast<char*>(data_.data()), bitStreamSize);
  if (!fin) { return false; }
  fin.close();
  return true;
}

bool
Bitstream::save(const std::string& compressedStreamPath) {
  std::ofstream fout(compressedStreamPath, std::ios::binary);
  if (!fout.is_open()) { return false; }
  fout.write(reinterpret_cast<const char*>(data_.data()), size());
  fout.close();
  return true;
}

void
Bitstream::read(std::vector<uint8_t>& buffer, size_t size) {
  buffer.resize(size);
  memcpy(buffer.data(), data_.data() + position_.bytes_, size);
  position_.bytes_ += size;
}

void
Bitstream::write(const std::vector<uint8_t>& buffer) {
  realloc(buffer.size());
  memcpy(data_.data() + position_.bytes_, buffer.data(), buffer.size());
  position_.bytes_ += buffer.size();
}

void
Bitstream::copyFrom(Bitstream&   srcBitstream,
                    const size_t position,
                    const size_t size) {
  if (data_.size() < position_.bytes_ + size) {
    data_.resize(position_.bytes_ + size);
  }
  memcpy(
    data_.data() + position_.bytes_, srcBitstream.buffer() + position, size);
  position_.bytes_ += size;
  auto pos = srcBitstream.getPosition();
  pos.bytes_ += size;
  srcBitstream.setPosition(pos);
}

void
Bitstream::copyTo(Bitstream& dstBitstream, const size_t size) {
  dstBitstream.initialize(dstBitstream.position_.bytes_ + size);
  memcpy(dstBitstream.buffer() + dstBitstream.position_.bytes_,
         data_.data() + position_.bytes_,
         size);
  position_.bytes_ += size;
}

void
Bitstream::computeMD5() {
  md5::MD5             md5Hash;
  std::vector<uint8_t> tmp_digest;
  tmp_digest.resize(16);
  size_t dataSize = size() == 0 ? data_.size() : size();
  md5Hash.update(data_.data(), dataSize);
  md5Hash.finalize(tmp_digest.data());
  printf("Bitstream md5:");
  for (auto& bitStr : tmp_digest) printf("%02x", bitStr);
  printf("\n");
  return;
}
