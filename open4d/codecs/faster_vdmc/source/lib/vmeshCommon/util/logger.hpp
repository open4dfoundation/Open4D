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
#pragma once

#include "../v3cBitstream/v3cCommon.hpp"
#include <string>

namespace vmesh {

enum LoggerType {
  LOG_DESCR = 0,     // *dsrc_bitstr.txt a short description of the bitstream	(mandatory)
  LOG_TRACE,         // *.trc	trace file	(optional)
  LOG_BITSTRMD5,     // *_bistream_md5.txt	bitstream md5	(mandatory)
  LOG_HLS,           // *_hls_md5.txt output high level syntax md5	(mandatory)
  LOG_PICTURE,       // *_picture_log.txt	output video frame log	(optional)
  LOG_BASEMESH,      // *_basemesh_log.txt	output basemesh frame log	(optional)
  LOG_DISPLACEMENT,  // *_displacement_log.txt	output displacement frame log	(optional)
  LOG_ATLAS,         // *_atlas_log.txt output atlas log	(optional)
  LOG_TILES,         // *_tile_log.txt	output tile group log	(optional)
  LOG_MFRAME,        // *_mframe_log.txt	output dynamic mesh frame log	(optional)
  LOG_RECFRAME,      // *_rec_pcframe_log.txt	output reconstructed point cloud frame log	(optional)
#if defined(BITSTREAM_TRACE)
  LOG_STREAM,
#endif
  LOG_ERROR,
};

static const std::string
get(LoggerType type) {
  switch (type) {
  case LOG_DESCR: return std::string("_dscr_bitstr.txt");
  case LOG_TRACE: return std::string(".trc");
  case LOG_BITSTRMD5: return std::string("_bitstream_md5.txt");
  case LOG_HLS: return std::string("_hls_md5.txt");
  case LOG_PICTURE: return std::string("_picture_log.txt");
  case LOG_BASEMESH: return std::string("_basemesh_log.txt");
  case LOG_DISPLACEMENT: return std::string("_ac_displacement_log.txt");
  case LOG_ATLAS: return std::string("_atlas_log.txt");
  case LOG_TILES: return std::string("_tile_log.txt");
  case LOG_MFRAME: return std::string("_mframe_log.txt");
  case LOG_RECFRAME: return std::string("_rec_mframe_log.txt");
#if defined(BITSTREAM_TRACE)
  case LOG_STREAM: return std::string("_bitstream.txt");
#endif
  default: return std::string("_unknow.txt");
  }
}

class VirtualLogger {
public:
  VirtualLogger() : file_(NULL), disable_(false) {}
  ~VirtualLogger() { close(); }
  bool initialize(LoggerType   type,
                  std::string& filename,
                  bool         encoder,
                  size_t       atlasId = 0) {
    std::string str = get(type);
    // size_t pos = str.find_last_of( "." );  //ajt::disables adding the atlasID to the file name based on Danillo's
    // comment if ( pos != std::string::npos ) str.insert( pos, std::to_string( atlasId ) );
    return open(filename + (encoder ? "_enc" : "_dec") + str);
  }
  inline bool isInitialized() { return file_ != NULL; }
  inline void disable() { disable_ = true; }
  inline void enable() { disable_ = false; }
  template<typename... Args>
  inline void trace(const char* format, Args... eArgs) {
    if (file_ && !disable_) { fprintf(file_, format, eArgs...); }
  }
  inline void flush() {
    if (!disable_) { fflush(file_); }
  }

private:
  void close() {
    if (file_) { fclose(file_); }
    file_ = NULL;
  }
  bool open(std::string name) {
    close();
    return ((file_ = fopen(name.c_str(), "w+")) != NULL);
  }
  FILE* file_;
  bool  disable_;
};

class Logger {
public:
  Logger() : filename_(""), encoder_(true) { logger_.resize(LOG_ERROR); }
  ~Logger() { logger_.clear(); }
  void initilalize(const std::string& filename, bool encoder = false) {
    filename_ = filename;
    encoder_  = encoder;
  }
  void         enable(LoggerType type) { logger_[type].enable(); }
  void         disable(LoggerType type) { logger_[type].disable(); }
  std::string& getLoggerBaseFileName() { return filename_; }

  template<typename... Args>
  inline void trace(LoggerType type, const char* format, Args... args) {
    if (!logger_[type].isInitialized()) {
      logger_[type].initialize(type, filename_, encoder_);
    }
    if (logger_[type].isInitialized()) {
      logger_[type].trace(format, args...);
      logger_[type].flush();
    }
  }
  template<typename... Args>
  inline void traceDescr(const char* format, Args... args) {
    trace(LOG_DESCR, format, args...);
  }
  template<typename... Args>
  inline void traceTrace(const char* format, Args... args) {
    trace(LOG_TRACE, format, args...);
  }
  template<typename... Args>
  inline void traceHLS(const char* format, Args... args) {
    trace(LOG_HLS, format, args...);
  }
  template<typename... Args>
  inline void traceAtlas(const char* format, Args... args) {
    trace(LOG_ATLAS, format, args...);
  }
  template<typename... Args>
  inline void traceTiles(const char* format, Args... args) {
    trace(LOG_TILES, format, args...);
  }
  template<typename... Args>
  inline void traceMFrame(const char* format, Args... args) {
    trace(LOG_MFRAME, format, args...);
  }
  template<typename... Args>
  inline void traceRecFrame(const char* format, Args... args) {
    trace(LOG_RECFRAME, format, args...);
  }
  template<typename... Args>
  inline void tracePicture(const char* format, Args... args) {
    trace(LOG_PICTURE, format, args...);
  }
  template<typename... Args>
  inline void traceBasemesh(const char* format, Args... args) {
      trace(LOG_BASEMESH, format, args...);
  }
  template<typename... Args>
  inline void traceDisplacement(const char* format, Args... args) {
      trace(LOG_DISPLACEMENT, format, args...);
  }
  template<typename... Args>
  inline void traceBitStreamMD5(const char* format, Args... args) {
    trace(LOG_BITSTRMD5, format, args...);
  }
#if defined(BITSTREAM_TRACE)
  template<typename... Args>
  inline void traceStream(const char* format, Args... args) {
    trace(LOG_STREAM, format, args...);
  }
  void traceStreamIndent() { trace(LOG_STREAM, "%*s", indent_, ""); }
#endif

  template<typename T>
  void traceVector(LoggerType        type,
                   std::vector<T>    data,
                   const size_t      width,
                   const size_t      height,
                   const std::string string,
                   const bool        hexa = false) {
    if (data.size() == 0) { data.resize(width * height, 0); }
    trace(type, "%s: %zu %zu \n", string.c_str(), width, height);
    for (size_t v0 = 0; v0 < height; ++v0) {
      for (size_t u0 = 0; u0 < width; ++u0) {
        trace(type, hexa ? "%2x" : "%3d", (int)(data[v0 * width + u0]));
      }
      trace(type, "\n");
    }
  }
  void indentIn() { indent_ += 2; }
  void indentOut() {
    if (indent_ > 1) indent_ -= 2;
  }
  uint32_t getIndent() const { return indent_; }

private:
  std::vector<VirtualLogger> logger_;
  std::string                filename_;
  uint32_t                   indent_ = 0;
  bool                       encoder_;
};

#if defined(BITSTREAM_TRACE)
#  define TRACE_BITSTREAM(fmt, ...) bitstream.trace(fmt, ##__VA_ARGS__);
#  define TRACE_BITSTREAM_IN(fmt, ...) bitstream.traceIn(fmt, ##__VA_ARGS__);
#  define TRACE_BITSTREAM_OUT(fmt, ...) bitstream.traceOut(fmt, ##__VA_ARGS__);
#else
#  define TRACE_BITSTREAM(fmt, ...)
#  define TRACE_BITSTREAM_IN(fmt, ...)
#  define TRACE_BITSTREAM_OUT(fmt, ...)
#endif

#if defined(CONFORMANCE_LOG)
#define TRACE_DESCRIPTION( fmt, ... ) logger_->traceDescr( fmt, ##__VA_ARGS__ );
#define TRACE_HLS( fmt, ... ) logger_->traceHLS( fmt, ##__VA_ARGS__ );
#define TRACE_ATLAS( fmt, ... ) logger_->traceAtlas( fmt, ##__VA_ARGS__ );
#define TRACE_TILE( fmt, ... ) logger_->traceTiles( fmt, ##__VA_ARGS__ );
#define TRACE_MFRAME( fmt, ... ) logger_->traceMFrame( fmt, ##__VA_ARGS__ );
#define TRACE_RECFRAME( fmt, ... ) logger_->traceRecFrame( fmt, ##__VA_ARGS__ );
#define TRACE_PICTURE( fmt, ... ) logger_->tracePicture( fmt, ##__VA_ARGS__ );
#define TRACE_BASEMESH( fmt, ... ) logger_->traceBasemesh( fmt, ##__VA_ARGS__ );
#define TRACE_DISPLACEMENT( fmt, ... ) logger_->traceDisplacement( fmt, ##__VA_ARGS__ );
#define TRACE_BITSTRMD5( fmt, ... ) loggerBitstream.traceBitStreamMD5( fmt, ##__VA_ARGS__ );
#else
#define TRACE_HLS( fmt, ... ) ;
#define TRACE_ATLAS( fmt, ... ) ;
#define TRACE_TILE( fmt, ... ) ;
#define TRACE_PCFRAME( fmt, ... ) ;
#define TRACE_RECFRAME( fmt, ... ) ;
#define TRACE_PICTURE( fmt, ... ) ;
#define TRACE_BITSTRMD5( fmt, ... ) ;
#endif
// borrowed from TMIV
// TODO: the error/conformance checking should extended

#if defined(__clang__) || defined(__GNUC__)
#  define LIKELY(x) __builtin_expect(static_cast<bool>(x), 1)
#else
#  define LIKELY(x) (static_cast<bool>(x))
#endif

class ConformanceError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

class SoftwareLimitationError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline auto
message(char const* introduction,
        char const* condition,
        char const* file,
        int32_t     line) -> std::string {
  std::ostringstream stream;
  stream << introduction << ":\n\t" << condition << "\n\t[" << file << "@"
         << line << "]\n";
  return stream.str();
}

[[noreturn]] inline void
confromanceError(char const* condition, char const* file, int32_t line) {
  throw ConformanceError{
    message("Not conformant to 1st edition of ISO/IEC 23090-29",
            condition,
            file,
            line)};
}

[[noreturn]] inline void
softwareLimitatonError(char const* condition, char const* file, int32_t line) {
  throw SoftwareLimitationError{
    message("Not align with software implementation limitation",
            condition,
            file,
            line)};
}

#define VERIFY_CONFORMANCE(condition) \
  static_cast<void>( \
    LIKELY(condition) \
    || (confromanceError(#condition, __FILE__, __LINE__), false))

#define VERIFY_SOFTWARE_LIMITATION(condition) \
  static_cast<void>( \
    LIKELY(condition) \
    || (softwareLimitatonError(#condition, __FILE__, __LINE__), false))

}  // namespace vmesh
