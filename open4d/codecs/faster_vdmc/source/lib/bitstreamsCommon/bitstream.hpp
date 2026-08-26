/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2025, ISO/IEC
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
#pragma once

#include "util/memory.hpp"
#include "util/logger.hpp"

namespace vmesh {

struct BistreamPosition {
  uint64_t bytes_;
  uint8_t  bits_;
};

class Bitstream {
public:
  Bitstream();
  ~Bitstream();

  bool initialize(std::vector<uint8_t>& data);
  bool initialize(const Bitstream& bitstream);
  void initialize(uint64_t capacity) { data_.resize(capacity, 0); }

  bool load(const std::string& compressedStreamPath);
  bool save(const std::string& compressedStreamPath);

  void clear() {
    data_.clear();
    position_.bits_  = 0;
    position_.bytes_ = 0;
  }
  void beginning() {
    position_.bits_  = 0;
    position_.bytes_ = 0;
  }
  uint8_t*                    buffer() { return data_.data(); }
  std::vector<uint8_t>&       vector() { return data_; }
  const std::vector<uint8_t>& vector() const { return data_; }
  uint64_t                    size() { return position_.bytes_; }
  uint64_t                    capacity() { return data_.size(); }
  BistreamPosition            getPosition() { return position_; }
  void       setPosition(BistreamPosition& val) { position_ = val; }
  Bitstream& operator+=(const uint64_t size) {
    position_.bytes_ += size;
    return *this;
  }
  void copyFromBits( uint8_t* buffer, const uint64_t startByte, const uint64_t bitstreamSize );
  void copyFromBits(Bitstream&   dataBitstream,
                     const size_t startByte,
                     const size_t bitstreamSize);
  void copyFrom(Bitstream&   dataBitstream,
                const size_t startByte,
                const size_t bitstreamSize);
  void copyFrom( uint8_t* buffer, const uint64_t startByte, const uint64_t bitstreamSize );
  void copyToBits( uint8_t* buffer, const uint64_t outputSize );
  void copyToBits(uint8_t* buffer, const uint64_t outputSize, const uint8_t bitsInLastByte);
  void copyToBits(Bitstream& dstBitstream, const size_t size);
  void copyTo(Bitstream& dataBitstream, const size_t size);
  void copyTo( uint8_t* buffer, const uint64_t startByte, const uint64_t outputSize );
  // so we make the class videoBitstream independent
//  void writeVideo(VideoBitstream& videoBitstream);
  void writeVideo(const uint8_t* data, size_t size);
//  void readVideo(VideoBitstream& videoBitstream, size_t videoStreamSize);
  void readVideo(uint8_t* buffer, size_t size);
  void read(std::vector<uint8_t>& buffer, size_t size);
  void write(const std::vector<uint8_t>& buffer);
  bool byteAligned() { return (position_.bits_ == 0); }
  bool moreData() { return position_.bytes_ < data_.size(); }
  void computeMD5();

  inline std::string readString() {
    while (!byteAligned()) { read(1); }
    std::string str;
    char        element = read(8);
    while (element != 0x00) {
      str.push_back(element);
      element = read(8);
    }
    return str;
  }

  inline void writeString(std::string str) {
    while (!byteAligned()) { write(0, 1); }
    for (auto& element : str) { write(element, 8); }
    write(0, 8);
  }

  inline uint32_t peekByteAt(uint64_t peekPos) { return data_[peekPos]; }
  inline uint32_t read(uint8_t bits) {
    uint32_t code = read(bits, position_);
    return code;
  }
  template<typename T>
  void write(T value, uint8_t bits) {
    write(static_cast<uint32_t>(value), bits, position_);
  }

  template<typename T>
  void write40bits(T value, uint8_t bits) {
    int64_t buffer40;
    memcpy(&buffer40, &value, sizeof(int64_t));
    int32_t buffer20Low = buffer40 & 0xFFFFF;
    write(buffer20Low, 20);
    int32_t buffer20High = (buffer40 >> 20) & 0xFFFFF;
    write(buffer20High, 20);
  }
  inline int64_t read40bits(uint8_t bits) {
    int32_t buffer20Low = read(20);
    int32_t buffer20High = read(20);
    int64_t buffer40;
    buffer40 = buffer20High;
    buffer40 = buffer40 << 20;
    buffer40 += buffer20Low;
    return buffer40;
  }

  inline void writeS(int32_t value, uint8_t bits) {
    assert(bits > 0);
    uint32_t code;
    if (value >= 0) {
      code = (uint32_t)value;
    } else {
      code = (uint32_t)(value & ((1 << (bits - 1)) - 1));
      code |= (1 << (bits - 1));
    }
    write(code, bits);
  }

  inline int32_t readS(uint8_t bits) {
    assert(bits > 0);
    uint32_t code = read(bits);
    int32_t  value;
    uint32_t midPoint = (1 << (bits - 1));
    if (code < midPoint) {
      value = (int32_t)code;
    } else {
      value = (int32_t)code | ~(midPoint - 1);
    }
    return value;
  }

  template<typename T>
  inline void writeUvlc(T value) {
    uint32_t code   = static_cast<uint32_t>(value);
    uint32_t length = 1, temp = ++code;
    while (1 != temp) {
      temp >>= 1;
      length += 2;
    }
    write(0, length >> 1);
    write(code, (length + 1) >> 1);
  }

  inline uint32_t readUvlc() {
    uint32_t value = 0, code = 0, length = 0;
    code = read(1);
    if (0 == code) {
      length = 0;
      while (!(code & 1)) {
        code = read(1);
        length++;
      }
      value = read(length);
      value += (1 << length) - 1;
    }
    return value;
  }

  template<typename T>
  inline void writeSvlc(T value) {
    int32_t code = static_cast<int32_t>(value);
    writeUvlc((uint32_t)(code <= 0 ? -code << 1 : (code << 1) - 1));
  }

  inline int32_t readSvlc() {
    uint32_t bits = readUvlc();
    return (bits & 1) ? (int32_t)(bits >> 1) + 1 : -(int32_t)(bits >> 1);
  }

  inline void writeFloat(float value) {
    uint32_t code = 0;
    memcpy(&code, &value, sizeof(float));
    write(code, 32);
  }

  inline float readFloat() {
    uint32_t code  = read(32);
    float    value = 0;
    memcpy(&value, &code, sizeof(float));
    return value;
  }

  inline void writeVu( uint32_t value ) {
    do {
      write( value & 0x7f, 7 );
      value >>=7;
      write( value != 0, 1 );
    } while( value != 0 );
  }

  inline int32_t readVu() {
    uint32_t value = 0;
    uint32_t index = 0;
    do {
      value |= (read(7) << (7 * index++));
    } while (read(1));
    return value;
  }

  inline void writeDouble(double value) {
      uint64_t buffer64;
      memcpy(&buffer64, &value, sizeof(double));
      uint32_t buffer32Low = buffer64 & 0xFFFFFFFF;
      write(buffer32Low, 32);
      uint32_t  buffer32High = (buffer64 >> 32) & 0xFFFFFFFF;
      write(buffer32High, 32);
  }

  inline double readDouble() {
      uint32_t buffer32Low = read(32);
      uint32_t buffer32High = read(32);
      uint64_t buffer64;
      buffer64 = buffer32High;
      buffer64 = buffer64 << 32;
      buffer64 += buffer32Low;
      double value = 0;
      memcpy(&value, &buffer64, sizeof(double));
      return value;
  }
#if defined(BITSTREAM_TRACE)
  void traceBitFloat(const std::string& name,
                     float              value,
                     const std::string& type) {
    if (trace_) {
      logger_->traceStream("[%6llu-%1u]", position_.bytes_, position_.bits_);
      logger_->traceStreamIndent();
      auto              str = name.substr(0, 50 - logger_->getIndent());
      static const char dots[] =
        "..................................................";
      auto len = (std::min)(logger_->getIndent() + str.length(), (size_t)50);
      logger_->traceStream(
        "%s %s = %-6f %s \n", str.c_str(), dots + len, value, type.c_str());
    }
  }
  void traceBitDouble(const std::string& name,
      double              value,
      const std::string& type) {
      if (trace_) {
          logger_->traceStream("[%6llu-%1u]", position_.bytes_, position_.bits_);
          logger_->traceStreamIndent();
          auto              str = name.substr(0, 50 - logger_->getIndent());
          static const char dots[] =
              "..................................................";
          auto len = (std::min)(logger_->getIndent() + str.length(), (size_t)50);
          logger_->traceStream(
              "%s %s = %-6f %s \n", str.c_str(), dots + len, value, type.c_str());
      }
  }
  void traceBitStr(const std::string& name,
                   const std::string& value,
                   const std::string& type) {
    if (trace_) {
      logger_->traceStream("[%6llu-%1u]", position_.bytes_, position_.bits_);
      logger_->traceStreamIndent();
      auto              str = name.substr(0, 50 - logger_->getIndent());
      static const char dots[] =
        "..................................................";
      auto len = (std::min)(logger_->getIndent() + str.length(), (size_t)50);
      logger_->traceStream("%s %s = %-6f %s \n",
                           str.c_str(),
                           dots + len,
                           value.c_str(),
                           type.c_str());
    }
  }
  template<typename T>
  void traceBit(const std::string& name, T value, const std::string& type) {
    if (trace_) {
      logger_->traceStream("[%6llu-%1u]", position_.bytes_, position_.bits_);
      logger_->traceStreamIndent();
      auto              str = name.substr(0, 50 - logger_->getIndent());
      static const char dots[] =
        "..................................................";
      auto len = (std::min)(logger_->getIndent() + str.length(), (size_t)50);
      logger_->traceStream("%s %s = %-6d %s \n",
                           str.c_str(),
                           dots + len,
                           (int)value,
                           type.c_str());
    }
  }

  template<typename... Args>
  void trace(const char* pFormat, Args... eArgs) {
    if (trace_) {
      logger_->traceStream("[%6llu-%1u]", position_.bytes_, position_.bits_);
      logger_->traceStreamIndent();
      logger_->traceStream(pFormat, eArgs...);
    }
  }
  template<typename... Args>
  void traceIn(const char* pFormat, Args... eArgs) {
    if (trace_) {
      trace(pFormat, eArgs...);
      logger_->traceStream("%s\n", "(){");
      //logger_->indentIn();
    }
  }
  template<typename... Args>
  void traceOut(const char* pFormat, Args... eArgs) {
    if (trace_) {
      //logger_->indentOut();
      trace("%s", "} //~");
      logger_->traceStream(pFormat, eArgs...);
      logger_->traceStream("%s\n", "");
    }
  }
  template<typename... Args>
  void traceBitStreamMD5(const char* pFormat, Args... eArgs) {
      if (trace_) {
          logger_->traceBitStreamMD5(pFormat, eArgs...);
      }
  }
  void  setTrace(bool trace) { trace_ = trace; }
  bool  getTrace() { return trace_; }
  void  setLogger(Logger& logger) {
    logger_ = &logger;
  }
  auto& getLogger() { return *logger_; }
#endif
private:
  inline void realloc(const size_t size = 4096) {
    data_.resize(data_.size() + (((size / 4096) + 1) * 4096));
  }
  inline uint32_t read(uint8_t bits, BistreamPosition& pos) {
    uint32_t value = 0;
    for (size_t i = 0; i < bits; i++) {
      value |= ((data_[pos.bytes_] >> (7 - pos.bits_)) & 1) << (bits - 1 - i);
      if (pos.bits_ == 7) {
        pos.bytes_++;
        pos.bits_ = 0;
      } else {
        pos.bits_++;
      }
    }
    return value;
  }

  inline void write(uint32_t value, uint8_t bits, BistreamPosition& pos) {
    if (pos.bytes_ + bits + 64 >= data_.size()) { realloc(); }
    for (size_t i = 0; i < bits; i++) {
      data_[pos.bytes_] |= ((value >> (bits - 1 - i)) & 1) << (7 - pos.bits_);
      if (pos.bits_ == 7) {
        pos.bytes_++;
        pos.bits_ = 0;
      } else {
        pos.bits_++;
      }
    }
  }

  std::vector<uint8_t> data_;
  BistreamPosition     position_;

#if defined(BITSTREAM_TRACE)
  bool    trace_;
  Logger* logger_ = nullptr;
#endif
};

}  // namespace vmesh
