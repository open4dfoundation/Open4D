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

#include <stdio.h>
#include "vmc.hpp"
//#include "vmc.hpp" // included twice

namespace mm {
class Image;
class Model;
}  // namespace mm

namespace vmesh {

class Checksum {
public:
  Checksum() = default;
  Checksum(size_t attCount) { attributes_.resize(attCount); }
  ~Checksum() = default;

  void allocate(size_t attCount) { attributes_.resize(attCount); }
  void add(const Sequence& sequence);
  template<typename T>
  void add(const TriangleMesh<T>& mesh);
  void add(const Frame<uint8_t>& texture, size_t attIdx);

  void print();
  template<typename T>
  void print(const TriangleMesh<T>& mesh, std::string eString);
  void print(const Frame<uint8_t>& texture, std::string eString);
  void print(const Frame<uint16_t>& texture, std::string eString);
  template<typename T>
  void print(const std::vector<Vec3<T>>& displacement, std::string eString);
  void printMeshChecksum(std::string eString);

  bool read(const std::string& path);
  bool write(const std::string& path);

  template<typename T>
  std::string getChecksum(const TriangleMesh<T>& mesh);
  std::string getChecksum(const Frame<uint8_t>& texture);
  std::string getChecksum(const Frame<uint16_t>& texture);
  std::string getChecksum(const mm::Model& mesh);
  std::string getChecksum(const mm::Image& texture);
  template<typename T>
  std::string getChecksum(const std::vector<Vec3<T>>& displacement);
  std::string getChecksum(const std::vector<uint8_t>& data);
  template<typename T>
  std::string getChecksumConformance(const TriangleMesh<T>& mesh); 
  template<typename T>
  std::string getChecksum(const Plane<T>& plane);

  bool operator!=(const Checksum& rhs) const { return !(*this == rhs); }

  //bool operator==(const Checksum& rhs) const;
  bool operator==(const Checksum& rhs) const {
    bool equal = true;
    equal &= mesh_.size() == rhs.mesh_.size();
    equal &= attributes_.size() == rhs.attributes_.size();
    if (!equal) {
      printf("number of meshes or attributes are different\n");
      return equal;
    }
    size_t attCount = attributes_.size();
    for (size_t attIdx = 0; attIdx < attCount; attIdx++) {
      equal &= attributes_[attIdx].size() == rhs.attributes_[attIdx].size();
      if (!equal) {
        printf("number of frames for attribut[%zu] are different\n", attIdx);
        return equal;
      }
    }

    size_t numMesh = mesh_.size();
    for (size_t i = 0; equal && (i < numMesh); i++) {
      equal &= mesh_[i] == rhs.mesh_[i];
      printf("Frame %4zu: [MD5GEO:", i);
      for (auto& c : mesh_[i]) { printf("%02x", c); }
      printf(",");
      for (auto& c : rhs.mesh_[i]) { printf("%02x", c); }
      printf("][%s]\n", mesh_[i] == rhs.mesh_[i] ? "EQUAL" : "DIFF");
    }
    for (size_t attIdx = 0; attIdx < attCount; attIdx++) {
      size_t frameCount = attributes_[attIdx].size();
      if (frameCount > 0) {
        for (size_t i = 0; equal && (i < frameCount); i++) {
          equal &= attributes_[attIdx][i] == rhs.attributes_[attIdx][i];
          printf("Frame %4zu: [MD5ATT%zu:", i, attIdx);
          for (auto& c : attributes_[attIdx][i]) { printf("%02x", c); }
          printf(",");
          for (auto& c : rhs.attributes_[attIdx][i]) { printf("%02x", c); }
          printf("][%s]\n",
                 attributes_[attIdx][i] == rhs.attributes_[attIdx][i]
                   ? "EQUAL"
                   : "DIFF");
        }
      }
    }
    return equal;
  }
  std::vector<std::vector<uint8_t>>& checksumMesh() { return mesh_; }
  std::vector<std::vector<std::vector<uint8_t>>>& checksumAtts() {
    return attributes_;
  }
  std::vector<std::vector<uint8_t>>& checksumAtt(size_t attIdx) {
    return attributes_[attIdx];
  }

private:
  //  template<typename T>
  //  void add(const TriangleMesh<T>& mesh);
  //  void add(const Frame<uint8_t>& texture);

  template<typename T>
  std::vector<uint8_t> compute(const TriangleMesh<T>& mesh);
  std::vector<uint8_t> compute(const Frame<uint8_t>& texture);
  std::vector<uint8_t> compute(const Frame<uint16_t>& texture);
  std::vector<uint8_t> compute(const mm::Model& mesh);
  std::vector<uint8_t> compute(const mm::Image& texture);
  template<typename T>
  std::vector<uint8_t> compute(const std::vector<Vec3<T>>& displacement);
  std::vector<uint8_t> compute(const std::vector<uint8_t>& data);
  template<typename T>
  std::vector<uint8_t> computeConformance(const TriangleMesh<T>& mesh);
  template<typename T>
  std::vector<uint8_t> compute(const Plane<T>& plane);
  std::vector<std::vector<uint8_t>>              mesh_;
  std::vector<std::vector<std::vector<uint8_t>>> attributes_;
};

}  // namespace vmesh
