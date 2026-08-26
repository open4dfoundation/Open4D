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
#include <stdio.h>
#include "vmc.hpp"
#include "checksum.hpp"
#include "MD5.h"

#include "mmModel.h"
#include "mmImage.h"

namespace vmesh {

//============================================================================

std::vector<uint8_t>
Checksum::compute(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest;
  md5::MD5             md5;
  md5.update(reinterpret_cast<const uint8_t*>(data.data()),
      (unsigned)(data.size() * sizeof(uint8_t)));
  digest.resize(MD5_DIGEST_STRING_LENGTH);
  md5.finalize(digest.data());
  return digest;
}

//============================================================================

template<typename T>
std::vector<uint8_t>
Checksum::compute(const std::vector<Vec3<T>>& displacement) {
  std::vector<uint8_t> digest;
  md5::MD5             md5;
  md5.update(reinterpret_cast<const uint8_t*>(displacement.data()),
             (unsigned)(displacement.size() * sizeof(Vec3<T>)));
  digest.resize(MD5_DIGEST_STRING_LENGTH);
  md5.finalize(digest.data());
  return digest;
}

//============================================================================

template<typename T>
std::vector<uint8_t>
Checksum::compute(const TriangleMesh<T>& mesh) {
  std::vector<uint8_t> digest;
  md5::MD5             md5;
  const auto&          points            = mesh.points();
  const auto&          colours           = mesh.colours();
  const auto&          texCoords         = mesh.texCoords();
  const auto&          normals           = mesh.normals();
  const auto&          triangles         = mesh.triangles();
  const auto&          texCoordTriangles = mesh.texCoordTriangles();
  const auto&          normalTriangles   = mesh.normalTriangles();
  const auto&          faceIds           = mesh.faceIds();
  md5.update(reinterpret_cast<const uint8_t*>(points.data()),
             unsigned(points.size() * sizeof(Vec3<T>)));
  if (!colours.empty())
    md5.update(reinterpret_cast<const uint8_t*>(colours.data()),
               unsigned(colours.size() * sizeof(Vec3<T>)));
  if (!texCoords.empty())
    md5.update(reinterpret_cast<const uint8_t*>(texCoords.data()),
               unsigned(texCoords.size() * sizeof(Vec2<T>)));
  if (!normals.empty())
    md5.update(reinterpret_cast<const uint8_t*>(normals.data()),
               unsigned(normals.size() * sizeof(Vec3<T>)));
  if (!triangles.empty())
    md5.update(reinterpret_cast<const uint8_t*>(triangles.data()),
               unsigned(triangles.size() * sizeof(Vec3<int>)));
  if (!texCoordTriangles.empty())
    md5.update(reinterpret_cast<const uint8_t*>(texCoordTriangles.data()),
               unsigned(texCoordTriangles.size() * sizeof(Vec3<int>)));
  if (!normalTriangles.empty())
    md5.update(reinterpret_cast<const uint8_t*>(normalTriangles.data()),
               unsigned(normalTriangles.size() * sizeof(Vec3<int>)));
  if (!faceIds.empty())
    md5.update(reinterpret_cast<const uint8_t*>(faceIds.data()),
               unsigned(faceIds.size() * sizeof(int)));
  digest.resize(MD5_DIGEST_STRING_LENGTH);
  md5.finalize(digest.data());
  return digest;
}

//============================================================================

template<typename T>
std::vector<uint8_t>
Checksum::computeConformance(const TriangleMesh<T>& mesh) {
    std::vector<uint8_t> digest;
    md5::MD5             md5;
    const auto& points = mesh.points();
    const auto& texCoords = mesh.texCoords();
    const auto& triangles = mesh.triangles();
    const auto& texCoordTriangles = mesh.texCoordTriangles();
    md5.update(reinterpret_cast<const uint8_t*>(points.data()),
        unsigned(points.size() * sizeof(Vec3<T>)));
    if (!triangles.empty())
        md5.update(reinterpret_cast<const uint8_t*>(triangles.data()),
            unsigned(triangles.size() * sizeof(Vec3<int>)));
    if (!texCoords.empty())
        md5.update(reinterpret_cast<const uint8_t*>(texCoords.data()),
            unsigned(texCoords.size() * sizeof(Vec2<T>)));
    if (!texCoordTriangles.empty())
        md5.update(reinterpret_cast<const uint8_t*>(texCoordTriangles.data()),
            unsigned(texCoordTriangles.size() * sizeof(Vec3<int>)));
    digest.resize(MD5_DIGEST_STRING_LENGTH);
    md5.finalize(digest.data());
    return digest;
}

//============================================================================

template<typename T>
std::vector<uint8_t>
Checksum::compute(const Plane<T>& plane) {
    std::vector<uint8_t> digest;
    md5::MD5             md5;
    auto size = plane.width() * plane.height();
    md5.update(reinterpret_cast<const uint8_t*>(plane.data()),
        size * sizeof(T));
    digest.resize(MD5_DIGEST_STRING_LENGTH);
    md5.finalize(digest.data());
    return digest;
}

//============================================================================

std::vector<uint8_t>
Checksum::compute(const Frame<uint8_t>& texture) {
  std::vector<uint8_t> digest;
  md5::MD5             md5;
  auto size0 = texture.plane(0).width() * texture.plane(0).height();
  auto size1 = texture.plane(1).width() * texture.plane(1).height();
  auto size2 = texture.plane(2).width() * texture.plane(2).height();
  md5.update(reinterpret_cast<const uint8_t*>(texture.plane(0).data()),
             size0 * sizeof(uint8_t));
  md5.update(reinterpret_cast<const uint8_t*>(texture.plane(1).data()),
             size1 * sizeof(uint8_t));
  md5.update(reinterpret_cast<const uint8_t*>(texture.plane(2).data()),
             size2 * sizeof(uint8_t));
  digest.resize(MD5_DIGEST_STRING_LENGTH);
  md5.finalize(digest.data());
  return digest;
}

//============================================================================

std::vector<uint8_t>
Checksum::compute(const Frame<uint16_t>& texture) {
  std::vector<uint8_t> digest;
  md5::MD5             md5;
  auto size0 = texture.plane(0).width() * texture.plane(0).height();
  auto size1 = texture.plane(1).width() * texture.plane(1).height();
  auto size2 = texture.plane(2).width() * texture.plane(2).height();
  md5.update(reinterpret_cast<const uint8_t*>(texture.plane(0).data()),
             size0 * sizeof(uint8_t));
  md5.update(reinterpret_cast<const uint8_t*>(texture.plane(1).data()),
             size1 * sizeof(uint8_t));
  md5.update(reinterpret_cast<const uint8_t*>(texture.plane(2).data()),
             size2 * sizeof(uint8_t));
  digest.resize(MD5_DIGEST_STRING_LENGTH);
  md5.finalize(digest.data());
  return digest;
}
//============================================================================

std::vector<uint8_t>
Checksum::compute(const mm::Model& mesh) {
  std::vector<uint8_t> digest;
  md5::MD5             md5;
  md5.update(reinterpret_cast<const uint8_t*>(mesh.vertices.data()),
             unsigned(mesh.vertices.size() * sizeof(float)));
  if (mesh.hasColors())
    md5.update(reinterpret_cast<const uint8_t*>(mesh.colors.data()),
               unsigned(mesh.colors.size() * sizeof(float)));
  if (mesh.hasUvCoords())
    md5.update(reinterpret_cast<const uint8_t*>(mesh.uvcoords.data()),
               unsigned(mesh.uvcoords.size() * sizeof(float)));
  if (mesh.hasNormals())
    md5.update(reinterpret_cast<const uint8_t*>(mesh.normals.data()),
               unsigned(mesh.normals.size() * sizeof(float)));
  if (mesh.hasTriangleNormals())
    md5.update(reinterpret_cast<const uint8_t*>(mesh.faceNormals.data()),
               unsigned(mesh.faceNormals.size() * sizeof(float)));
  if (mesh.hasTriangles())
    md5.update(reinterpret_cast<const uint8_t*>(mesh.triangles.data()),
               unsigned(mesh.triangles.size() * sizeof(int)));
  if (mesh.hasUvCoords())
    md5.update(reinterpret_cast<const uint8_t*>(mesh.trianglesuv.data()),
               unsigned(mesh.trianglesuv.size() * sizeof(int)));
  digest.resize(MD5_DIGEST_STRING_LENGTH);
  md5.finalize(digest.data());
  return digest;
}

//============================================================================

std::vector<uint8_t>
Checksum::compute(const mm::Image& texture) {
  std::vector<uint8_t> digest;
  md5::MD5             md5;
  auto                 size = texture.width * texture.height * texture.nbc;
  md5.update(reinterpret_cast<const uint8_t*>(texture.data),
             size * sizeof(uint8_t));
  digest.resize(MD5_DIGEST_STRING_LENGTH);
  md5.finalize(digest.data());
  return digest;
}

//============================================================================
template<typename T>
void
Checksum::add(const TriangleMesh<T>& mesh) {
  auto md5 = compute(mesh);
  mesh_.push_back(md5);
}

//============================================================================

void
Checksum::add(const Frame<uint8_t>& texture, size_t attIdx) {
  auto md5 = compute(texture);
  attributes_[attIdx].push_back(md5);
}

//============================================================================

void
Checksum::add(const Sequence& sequence) {
  for (int32_t i = 0; i < sequence.frameCount(); i++) {
    add(sequence.mesh(i));
  }
  for (size_t attIdx = 0; attIdx < sequence.attributes().size(); attIdx++) {
    if (sequence.attribute((int)attIdx).frameCount() > 0) {
      for (size_t i = 0; i < sequence.attribute((int)attIdx).frameCount();
           i++) {
        add(sequence.attributeFrame((int)attIdx, i), attIdx);
      }
    }
  }
}

//============================================================================

std::string
toString(const std::vector<uint8_t>& checksum) {
  std::string str;
  for (auto& c : checksum) {
    char data[8];
    snprintf(data, sizeof(data), "%02x", c);
    str += data;
  }
  return str;
}

//============================================================================

template<typename T>
std::string
Checksum::getChecksum(const Plane<T>& plane) {
    return toString(compute(plane));
}

//============================================================================

template<typename T>
std::string
Checksum::getChecksumConformance(const TriangleMesh<T>& mesh) {
    return toString(computeConformance(mesh));
}

//============================================================================

std::string
Checksum::getChecksum(const std::vector<uint8_t>& data) {
    return toString(compute(data));
}

//============================================================================
template<typename T>
std::string
Checksum::getChecksum(const std::vector<Vec3<T>>& displacement) {
  std::string str      = "";
  auto        checksum = compute(displacement);
  for (auto& c : checksum) {
    char data[8];
    snprintf(data, sizeof(data), "%02x", c);
    str += data;
  }
  return str;
}

//============================================================================

template<typename T>
std::string
Checksum::getChecksum(const TriangleMesh<T>& mesh) {
  return toString(compute(mesh));
}

//============================================================================

std::string
Checksum::getChecksum(const Frame<uint8_t>& texture) {
  return toString(compute(texture));
}
//============================================================================

std::string
Checksum::getChecksum(const Frame<uint16_t>& texture) {
  return toString(compute(texture));
}

//============================================================================

std::string
Checksum::getChecksum(const mm::Model& mesh) {
  return toString(compute(mesh));
}

//============================================================================

std::string
Checksum::getChecksum(const mm::Image& texture) {
  return toString(compute(texture));
}

//============================================================================

bool
Checksum::read(const std::string& path) {
  std::ifstream fin(path, std::ios::in);
  if (!fin.is_open()) return false;
  std::string         token;
  size_t              meshFrames     = 0;
  size_t              attributeCount = 0;
  std::vector<size_t> attributeFrames;
  size_t              sizeChecksumMesh = 0;
  std::vector<size_t> sizeChecksumAttributes;
  fin >> token;
  if (token != "meshFrames") return false;
  fin >> token;
  meshFrames = std::atoi(token.c_str());
  fin >> token;
  if (token != "attributeCount") return false;
  fin >> token;
  attributeCount = std::atoi(token.c_str());
  attributes_.resize(attributeCount);
  for (size_t attIdx = 0; attIdx < attributes_.size(); attIdx++) {
    fin >> token;
    if (token != "attributeFrames") return false;
    fin >> token;
    attributeFrames.push_back(std::atoi(token.c_str()));
  }

  fin >> sizeChecksumMesh;
  for (size_t attIdx = 0; attIdx < attributes_.size(); attIdx++) {
    size_t sizeChecksumTexture;
    fin >> sizeChecksumTexture;
    sizeChecksumAttributes.push_back(sizeChecksumTexture);
  }

  mesh_.resize(meshFrames);
  for (size_t i = 0; i < mesh_.size(); i++) {
    mesh_[i].resize(sizeChecksumMesh, 0);
    for (auto& c : mesh_[i]) {
      uint8_t c0;
      uint8_t c1;
      fin >> c0 >> c1;
      c = ((c0 + (c0 > '9' ? 9 : 0)) & 0x0F) * 16
          + ((c1 + (c1 > '9' ? 9 : 0)) & 0x0F);
    }
  }

  for (size_t attIdx = 0; attIdx < attributes_.size(); attIdx++) {
    auto&  texture_            = attributes_[attIdx];
    size_t numberOfTextures    = attributeFrames[attIdx];
    size_t sizeChecksumTexture = sizeChecksumAttributes[attIdx];
    texture_.resize(numberOfTextures);
    for (size_t i = 0; i < texture_.size(); i++) {
      texture_[i].resize(sizeChecksumTexture, 0);
      for (auto& c : texture_[i]) {
        uint8_t c0;
        uint8_t c1;
        fin >> c0 >> c1;
        c = ((c0 + (c0 > '9' ? 9 : 0)) & 0x0F) * 16
            + ((c1 + (c1 > '9' ? 9 : 0)) & 0x0F);
      }
    }
  }
  fin.close();
  return true;
}

//============================================================================

bool
Checksum::write(const std::string& path) {
  std::ofstream fout(path, std::ios::out);
  if (!fout.is_open()) { return false; }
  fout << "meshFrames\t" << mesh_.size() << std::endl;
  fout << "attributeCount\t" << attributes_.size() << std::endl;
  for (size_t attIdx = 0; attIdx < attributes_.size(); attIdx++) {
    fout << "attributeFrames\t" << attributes_[attIdx].size() << std::endl;
  }
  fout << (mesh_.empty() ? 0 : mesh_[0].size()) << std::endl;
  for (size_t attIdx = 0; attIdx < attributes_.size(); attIdx++)
    fout << (attributes_[attIdx].empty() ? 0 : attributes_[attIdx][0].size())
         << std::endl;
  for (size_t i = 0; i < mesh_.size(); i++) {
    for (auto& c : mesh_[i])
      fout << std::hex << (c / 16) << std::hex << (c % 16);
    fout << std::endl;
  }
  for (size_t attIdx = 0; attIdx < attributes_.size(); attIdx++) {
    auto& texture_ = attributes_[attIdx];
    for (size_t i = 0; i < texture_.size(); i++) {
      for (auto& c : texture_[i])
        fout << std::hex << (c / 16) << std::hex << (c % 16);
      fout << std::endl;
    }
  }
  fout.close();
  return true;
}

//============================================================================

template<typename T>
void
Checksum::print(const std::vector<Vec3<T>>& displacement,
                std::string                 eString) {
  printf(
    "Checksum %s: %s \n", eString.c_str(), getChecksum(displacement).c_str());
}

//============================================================================

template<typename T>
void
Checksum::print(const TriangleMesh<T>& mesh, std::string eString) {
  printf("Checksum %s: %s \n", eString.c_str(), getChecksum(mesh).c_str());
}

//============================================================================

void
Checksum::print(const Frame<uint8_t>& texture, std::string eString) {
  printf("Checksum %s: %s \n", eString.c_str(), getChecksum(texture).c_str());
}
//============================================================================

void
Checksum::print(const Frame<uint16_t>& texture, std::string eString) {
  printf("Checksum %s: %s \n", eString.c_str(), getChecksum(texture).c_str());
}
//============================================================================

void
Checksum::printMeshChecksum(std::string eString) {
  for (size_t i = 0; i < mesh_.size(); i++) {
    printf("%s [%zu] ", eString.c_str(), i);
    if (mesh_.size() > 0)
      for (auto& c : mesh_[i]) { printf("%02x", c); }
    printf("\n");
  }
}

//============================================================================

void
Checksum::print() {
  if (attributes_.size() == 1) {
    auto& texture_ = attributes_[0];
    for (size_t i = 0; i < mesh_.size(); i++) {
      printf("Frame %4zu: [MD5GEO:%32s][MD5TEX:%32s] \n",
             i,
             i < mesh_.size() ? toString(mesh_[i]).c_str() : "",
             i < texture_.size() ? toString(texture_[i]).c_str() : "");
    }
    return;
  }
  for (size_t i = 0; i < mesh_.size(); i++) {
    printf("Frame %4zu: [MD5GEO:%32s]\n",
           i,
           i < mesh_.size() ? toString(mesh_[i]).c_str() : "");
  }
  for (size_t attIdx = 0; attIdx < attributes_.size(); attIdx++) {
    for (size_t i = 0; i < attributes_[attIdx].size(); i++) {
      printf("Frame %4zu: [MD5ATT(%zu):%32s] \n",
             i,
             attIdx,
             i < attributes_[attIdx].size()
               ? toString(attributes_[attIdx][i]).c_str()
               : "");
    }
  }
}

//============================================================================

//bool
//Checksum::operator==(const Checksum& rhs) const {
//  const size_t numMin = (std::min)(mesh_.size(), rhs.mesh_.size());
//  const size_t numMax = (std::max)(mesh_.size(), rhs.mesh_.size());
//  bool         equal  = true;
//  equal &= mesh_.size() == rhs.mesh_.size();
//  equal &= texture_.size() == rhs.texture_.size();
//  for (size_t i = 0; equal && (i < numMin); i++) {
//    equal &= mesh_[i] == rhs.mesh_[i];
//    equal &= texture_[i] == rhs.texture_[i];
//  }
//  for (size_t i = 0; i < numMax; i++) {
//    printf("Frame %4zu: [MD5GEO:%32s,%32s][MD5TEX:%32s,%32s][%s,%s]\n",
//           i,
//           i < mesh_.size() ? toString(mesh_[i]).c_str() : "",
//           i < rhs.mesh_.size() ? toString(rhs.mesh_[i]).c_str() : "",
//           i < texture_.size() ? toString(texture_[i]).c_str() : "",
//           i < rhs.texture_.size() ? toString(rhs.texture_[i]).c_str() : "",
//           i < numMin && mesh_[i] == rhs.mesh_[i] ? "EQUAL" : "DIFF",
//           i < numMin && texture_[i] == rhs.texture_[i] ? "EQUAL" : "DIFF");
//  }
//  return equal;
//}

//============================================================================

template void Checksum::print<float>(const TriangleMesh<float>&, std::string);
template void Checksum::print<double>(const TriangleMesh<double>&,
                                      std::string);
template void Checksum::print<int32_t>(const TriangleMesh<int32_t>&,
                                       std::string);

template std::string
Checksum::getChecksum<float>(const TriangleMesh<float>& mesh);
template std::string
Checksum::getChecksum<double>(const TriangleMesh<double>& mesh);
template std::string
Checksum::getChecksum<int32_t>(const TriangleMesh<int32_t>& mesh);
template std::string
Checksum::getChecksumConformance<float>(const TriangleMesh<float>& mesh);
template std::string
Checksum::getChecksumConformance<double>(const TriangleMesh<double>& mesh);
template std::string
Checksum::getChecksumConformance<int32_t>(const TriangleMesh<int32_t>& mesh);
template std::string
Checksum::getChecksum<uint8_t>(const Plane<uint8_t>& mesh);
template std::string
Checksum::getChecksum<uint16_t>(const Plane<uint16_t>& mesh);
template std::string
Checksum::getChecksum<uint32_t>(const Plane<uint32_t>& mesh);

template std::vector<uint8_t>
Checksum::compute<float>(const TriangleMesh<float>&);
template std::vector<uint8_t>
Checksum::compute<double>(const TriangleMesh<double>&);
template std::vector<uint8_t>
Checksum::compute<int32_t>(const TriangleMesh<int32_t>&);
template std::vector<uint8_t>
Checksum::compute<uint8_t>(const Plane<uint8_t>&);
template std::vector<uint8_t>
Checksum::compute<uint16_t>(const Plane<uint16_t>&);
template std::vector<uint8_t>
Checksum::compute<uint32_t>(const Plane<uint32_t>&);

template void Checksum::add<float>(const TriangleMesh<float>&);
template void Checksum::add<double>(const TriangleMesh<double>&);
template void Checksum::add<int32_t>(const TriangleMesh<int32_t>&);

template std::string
Checksum::getChecksum<float>(const std::vector<Vec3<float>>& displacement);
template std::string
Checksum::getChecksum<double>(const std::vector<Vec3<double>>& displacement);
template void Checksum::print<float>(const std::vector<Vec3<float>>&,
                                     std::string);
template void Checksum::print<double>(const std::vector<Vec3<double>>&,
                                      std::string);
//============================================================================

}  // namespace vmesh
