
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
#pragma once

#include "util/image.hpp"

struct TestParameters {
  bool        verbose;
  std::string meshPath;
};

struct DisableSubProcessLog {
private:
  bool              disableLog_ = true;
  bool              isDisable_  = false;
  std::stringstream binCoutStream_;
  std::stringstream binCerrStream_;
  std::streambuf*   oldCoutRdBuf_{};
  std::streambuf*   oldCerrRdBuf_{};
  char              binStdoutBuffer_[4096]{};
  char              binStderrBuffer_[4096]{};
  FILE*             binStdout_{};
  FILE*             binStderr_{};
  FILE*             oldStdout_{};
  FILE*             oldStderr_{};

public:
  void switchOnLog() { disableLog_ = false; }
  bool disableLog() const { return disableLog_; }

#if defined(WIN32)
  void disable() {}
  void enable() {}
#else
  void disable() {
    if (disableLog_ && !isDisable_) {
      // Redirect std::cout and std::cerr
      oldCoutRdBuf_ = std::cout.rdbuf(binCoutStream_.rdbuf());
      oldCerrRdBuf_ = std::cerr.rdbuf(binCerrStream_.rdbuf());

      // redirect printf
      binStdout_ = fmemopen(binStdoutBuffer_, 4096, "w");
      binStderr_ = fmemopen(binStderrBuffer_, 4096, "w");
      if (binStdout_ == nullptr) { return; }
      if (binStderr_ == nullptr) { return; }
      oldStdout_ = stdout;
      oldStderr_ = stderr;
      stdout     = binStdout_;
      stderr     = binStderr_;
      isDisable_ = true;
    }
  }
  void enable() {
    if (isDisable_) {
      std::cout.rdbuf(oldCoutRdBuf_);
      std::cerr.rdbuf(oldCerrRdBuf_);
      std::fclose(binStdout_);
      std::fclose(binStderr_);
      stdout     = oldStdout_;
      stderr     = oldStderr_;
      isDisable_ = false;
    }
  }
#endif
};

static std::string
grep(const std::string& filename, const std::string& keyword) {
  std::ifstream in(filename.c_str());
  if (in.is_open()) {
    std::string line;
    while (getline(in, line)) {
      std::size_t pos = line.find(keyword);
      if (pos != std::string::npos) {
        in.close();
        return line.substr(pos + keyword.size());
      }
    }
  }
  in.close();
  return {};
}

static inline bool
exists(const std::string& name) {
  std::ifstream file(name.c_str());
  return file.good();
}

static inline size_t
hash(const std::string& name) {
  std::ifstream file(name);
  if (file.is_open()) {
    std::string str((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
    file.close();
    return std::hash<std::string>{}(str);
  }
  printf("Can not open: %s \n", name.c_str());
  return 0xffffffffffffffff;
}

#if defined(WIN32)
const std::string g_hmEncoderPath =
  "externaltools\\hm-16.21+scm-8.8\\bin\\vs17\\msvc-19.32\\x86_"
  "64\\release\\TAppEncoder.exe";
const std::string g_hmDecoderPath =
  "externaltools\\hm-16.21+scm-8.8\\bin\\vs17\\msvc-19.32\\x86_"
  "64\\release\\TAppDecoder.exe";
const std::string g_hdrConvertPath =
  "externaltools\\hdrtools\\build\\bin\\Release\\HDRConvert.exe";
const std::string g_dracoEncoderPath =
  "build\\Release\\bin\\Release\\draco_encoder.exe";
const std::string g_dracoDecoderPath =
  "build\\Release\\bin\\Release\\draco_decoder.exe";
const std::string g_mmMetricsPath = "build\\Release\\bin\\Release\\mm.exe";
const std::string g_vmeshEncodePath =
  "build\\Release\\bin\\Release\\encode.exe";
const std::string g_vmeshDecodePath =
  "build\\Release\\bin\\Release\\decode.exe";
#elif defined(__APPLE__)
const std::string g_hmEncoderPath = "externaltools/hm-16.21+scm-8.8/bin/umake/"
                                    "clang-13.1/x86_64/release/TAppEncoder";
const std::string g_hmDecoderPath = "externaltools/hm-16.21+scm-8.8/bin/umake/"
                                    "clang-13.1/x86_64/release/TAppDecoder";
const std::string g_hdrConvertPath =
  "externaltools/hdrtools/build/bin/HDRConvert";
const std::string g_dracoEncoderPath = "build/Release/bin/draco_encoder";
const std::string g_dracoDecoderPath = "build/Release/bin/draco_decoder";
const std::string g_mmMetricsPath    = "build/Release/bin/mm";
const std::string g_vmeshEncodePath  = "build/Release/bin/encode";
const std::string g_vmeshDecodePath  = "build/Release/bin/decode";
#else
const std::string g_hmEncoderPath =
  "externaltools/hm-16.21+scm-8.8/bin/TAppEncoderStatic";
const std::string g_hmDecoderPath =
  "externaltools/hm-16.21+scm-8.8/bin/TAppDecoderStatic";
const std::string g_hdrConvertPath =
  "externaltools/hdrtools/build/bin/HDRConvert";
const std::string g_dracoEncoderPath = "build/Release/bin/draco_encoder";
const std::string g_dracoDecoderPath = "build/Release/bin/draco_decoder";
const std::string g_mmMetricsPath    = "build/Release/bin/mm";
const std::string g_vmeshEncodePath  = "build/Release/bin/encode";
const std::string g_vmeshDecodePath  = "build/Release/bin/decode";
#endif

static bool
checkSoftwarePath() {
  bool ret          = true;
  auto externalPath = {g_hmEncoderPath,
                       g_hmDecoderPath,
                       g_hdrConvertPath,
                       g_dracoEncoderPath,
                       g_dracoDecoderPath,
                       g_mmMetricsPath,
                       g_vmeshEncodePath,
                       g_vmeshDecodePath};
  for (const auto& path : externalPath) {
    if (!exists(path)) {
      printf("Software path not exists: %s \n", path.c_str());
      ret = false;
    }
  }
  if (!ret) {
    printf("External softwares not installed: \n\n");
    printf(
      "  Please run \"./scripts/get_external_tools.sh --all\" script. \n\n");
    exit(-1);
  }
  return ret;
}

extern DisableSubProcessLog disableSubProcessLog;
extern TestParameters       testParams;