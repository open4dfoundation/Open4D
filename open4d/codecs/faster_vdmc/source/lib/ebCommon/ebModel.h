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

#ifndef _EB_MODEL_H_
#define _EB_MODEL_H_

//
#include <array>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <fstream>
// mathematics
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
// internal
#include "ebGeometry.h"

#define UVPRED_FRACBITS                   8

const int GEO_SHIFT = 5;
const int GEO_SCALE = (1 << GEO_SHIFT);
const int64_t NRM_SHIFT_1 = 40;

namespace eb {



enum class AttrDomain {
    PER_VERTEX = 0,
    PER_FACE = 1,
};

enum class AttrType {
    POSITION = -1, 
    TEXCOORD = 0,
    COLOR = 1,
    NORMAL = 2,
    MATERIAL_ID = 3, 
    GENERIC = 4     // CUSTOM ?
};
enum class DataType { // implementation could be extended to include unsigned integers
    INT32 = 0,
    INT64 = 1,
    FLOAT32 = 2,
    FLOAT64 = 3
};

static std::ostream&
operator<<(std::ostream& out, AttrDomain val) {
    switch (val) {
    case AttrDomain::PER_VERTEX: out << "V"; break;
    case AttrDomain::PER_FACE:   out << "F"; break;
    }
    return out;
}

static std::istream&
operator>>(std::istream& in, AttrDomain& type) {
    std::string str;
    in >> str;
    if      (str == "V") type = AttrDomain::PER_VERTEX;
    else if (str == "F") type = AttrDomain::PER_FACE;
    // else error
    return in;
}


static std::ostream&
operator<<(std::ostream& out, AttrType val) {
    switch (val) {
    case AttrType::POSITION:    out << "POS"; break;
    case AttrType::TEXCOORD:    out << "TXC"; break;
    case AttrType::COLOR:       out << "COL"; break;
    case AttrType::NORMAL:      out << "NOR"; break;
    case AttrType::MATERIAL_ID: out << "MID"; break;
    case AttrType::GENERIC:     out << "GEN"; break;
    }
    return out;
}

static std::istream&
operator>>(std::istream& in, AttrType& type) {
    std::string str;
    in >> str;
    if      (str == "POS") type = AttrType::POSITION;
    else if (str == "TXC") type = AttrType::TEXCOORD;
    else if (str == "COL") type = AttrType::COLOR;
    else if (str == "NOR") type = AttrType::NORMAL;
    else if (str == "MID") type = AttrType::MATERIAL_ID;
    else if (str == "GEN") type = AttrType::GENERIC;
    // else error
    return in;
}

static std::ostream&
operator<<(std::ostream& out, DataType val) {
    switch (val) {
    case DataType::INT32:    out << "I32"; break;
    case DataType::INT64:    out << "I64"; break;
    case DataType::FLOAT32:  out << "F32"; break;
    case DataType::FLOAT64:  out << "F64"; break;
    }
    return out;
}

static std::istream&
operator>>(std::istream& in, DataType& type) {
    std::string str;
    in >> str;
    if      (str == "I32") type = DataType::INT32;
    if      (str == "I64") type = DataType::INT64;
    else if (str == "F32") type = DataType::FLOAT32;
    else if (str == "F64") type = DataType::FLOAT64;
    // else error
    return in;
}


// https://stackoverflow.com/questions/39288891/why-is-shared-ptrvoid-legal-while-unique-ptrvoid-is-ill-formed
using unique_void_ptr = std::unique_ptr<void, void(*)(void const*)>;

template<typename T>
auto unique_void(T* ptr) -> unique_void_ptr
{
    return unique_void_ptr(ptr, [](void const* data) {
        T const* p = static_cast<T const*>(data);
        delete p;
        });
}
// should not be used like this - here copy from preexisting arrays
template<typename T>
auto make_unique_void(const T& org)
{
    return unique_void(new T(org));
}

class MeshAttribute {
    friend class Model;
public:
    template<typename T, typename U>
    MeshAttribute(AttrType t, DataType dt, size_t d, AttrDomain dom, const T& values, const U& indices, int ir = -1) :
        type(t), dataType(dt), dim(d), domain(dom), dataPtr(make_unique_void(values)), indexPtr(make_unique_void(indices)), indexRef(ir){
        dataCnt = values.size() / dim; // assumes values vector size is a multiple of dim - no resize
        indexCnt = indices.size() / 3; // assumes index size > 0 and index ref -1 // assumes triangles
    };

    template<typename T, typename U>
    MeshAttribute(AttrType t, DataType dt, size_t d, AttrDomain dom, T* values, const U& indices, int ir = -1) :
        type(t), dataType(dt), dim(d), domain(dom), dataPtr(unique_void(values)), indexPtr(make_unique_void(indices)), indexRef(ir) {
        dataCnt = values->size() / dim; // assumes values vector size is a multiple of dim - no resize
        indexCnt = indices.size() / 3; // assumes index size > 0 and index ref -1 // assumes triangles
    };

    template<typename T, typename U>
    MeshAttribute(AttrType t, DataType dt, size_t d, AttrDomain dom, T* values, U* indices, int ir = -1) :
        type(t), dataType(dt), dim(d), domain(dom), dataPtr(unique_void(values)), indexPtr(unique_void(indices)), indexRef(ir) {
        dataCnt = values->size() / dim; // assumes values vector size is a multiple of dim - no resize
        indexCnt = indices->size() / 3; // assumes index size > 0 and index ref -1 // assumes triangles
    };

    MeshAttribute(AttrType t, DataType dt, size_t d, AttrDomain dom, size_t valuesCnt, size_t indicesCnt, int ir = -1) :
        type(t), dataType(dt), dim(d), domain(dom), dataCnt(valuesCnt), indexCnt(indicesCnt), indexRef(ir) {
        //dataPtr indexPtr to be filled later.. or initialize directly ?
    };

    template<typename T>
    MeshAttribute(AttrType t, DataType dt, size_t d, AttrDomain dom, const T& values, int ir = -1) :
        type(t), dataType(dt), dim(d), domain(dom), dataPtr(make_unique_void(values)),indexRef(ir)
    {
        dataCnt = values.size() / dim;
    };

    template<typename T>
    MeshAttribute(AttrType t, DataType dt, size_t d, AttrDomain dom, T* values, int ir = -1) :
        type(t), dataType(dt), dim(d), domain(dom), dataPtr(unique_void(values)), indexRef(ir)
    {
        dataCnt = values->size() / dim;
    };

    MeshAttribute(const MeshAttribute&) = delete;
    MeshAttribute(MeshAttribute&& o) noexcept :
        type(std::move(o.type)),
        dataType(std::move(o.dataType)),
        dim(std::move(o.dim)),
        domain(std::move(o.domain)),
        dataCnt(std::move(o.dataCnt)),
        dataPtr(std::move(o.dataPtr)),
        indexCnt(std::move(o.indexCnt)),
        indexPtr(std::move(o.indexPtr)),
        indexRef(std::move(o.indexRef))
    {};

    template<typename T>
    void setData(const T& org)
    {
        dataPtr = make_unique_void(org);
        dataCnt = org.size() / dim;
    };
    
    template<typename T>
    void setData(T* org)
    {
        dataPtr = unique_void(org);
        dataCnt = org->size() / dim;
    };
    
    template<typename T>
    void setIndices(const T& org)
    {
        indexPtr = make_unique_void(org);
        indexCnt = org.size() / 3; // assumes triangles and indices multple of 3
    };

    template<typename T>
    void setIndices(T* org)
    {
        indexPtr = unique_void(org);
        indexCnt = org->size() / 3; // assumes triangles and indices multple of 3
    };

    void printHeader(std::ofstream& fout) const
    {
        // add text metadata , name ++ 
        fout << type << " " << dataType << " " << dim << " " << domain << " "
             << dataCnt << " " << indexRef << " " << indexCnt << "\n";
    }

    template<typename T>
    std::vector<T>* getData(void) const
    {
        return reinterpret_cast<std::vector<T>*>(dataPtr.get());
    };
    template<typename T>
    std::vector<T>* getIndices(void) const
    {
        return reinterpret_cast<std::vector<T>*>(indexPtr.get());
    };
    AttrType getType(void) const { return type; };
    DataType getDataType(void) const { return dataType; };
    AttrDomain getDomain(void) const { return domain; };
    size_t getDim(void) const { return dim; };
    int getIndexRef(void) const { return indexRef; };
    size_t getDataCnt(void) const { return dataCnt; };
    size_t getIndexCnt(void) const { return indexCnt; };

    const unique_void_ptr& getDataPtr(void) const { return dataPtr; };
    const unique_void_ptr& getIndexPtr(void) const { return indexPtr; };

    void setDataCnt(size_t count) { dataCnt = count; }; // use with caution 
    void setIndexRef(int idx) { indexRef = idx; }; // use with caution 

private:

    static  void nullDeleter(void const*) {};
    AttrType   type;
    DataType   dataType;
    size_t     dim = 0;
    AttrDomain domain;
    size_t     dataCnt = 0;
    unique_void_ptr      dataPtr = unique_void_ptr(nullptr, &nullDeleter);
    size_t     indexCnt = 0;
    unique_void_ptr     indexPtr = unique_void_ptr(nullptr, &nullDeleter);
    int        indexRef = -1;
};


// 3D Model: mesh or point cloud
class Model {
 public:
  std::string              header;       // mostly for OBJ material
  std::vector<std::string> comments;     // mostly for PLY
  std::vector<std::shared_ptr<MeshAttribute>>   attributes;

  // ctor
  Model() {}

  Model(const Model& o):
      header(o.header), comments(o.comments)
  {
      // deep copy of attribute data and indices vectors
      for (const auto& attrPtr : o.attributes)
      {
          const auto& attr = *attrPtr.get();
          attributes.emplace_back(
              std::make_shared<MeshAttribute>(attr.type, attr.dataType, attr.dim, attr.domain,attr.dataCnt, attr.indexCnt, attr.indexRef)
          );
          auto& mattr = *attributes.back().get();
          switch (attr.getDataType()) {
          case DataType::INT32:
              mattr.setData(*attr.getData<int32_t>());
              break;
          case DataType::INT64:
              mattr.setData(*attr.getData<int64_t>());
              break;
          case DataType::FLOAT32:
              mattr.setData(*attr.getData<float>());
              break;
          case DataType::FLOAT64:
              mattr.setData(*attr.getData<double>());
              break;
          }
          // only uint32_t as indices !! 
          if ((attr.indexRef == -1) || (attr.indexCnt > 0)) // we should not have index ref >= 0 and count >0
          {
              const auto indicesPtr = attr.getIndices<int32_t>();
              if (indicesPtr)
                mattr.setIndices(*indicesPtr);
          }
      }
  }
  // purge everything
  inline void reset( void ) {
    header = "";
    comments.clear();
    attributes.clear();
  }

  // a model that has at least vertices but no topology
  bool isPointCloud( void ) const { return hasVertices() && !hasTriangles(); }
  // a model that has at least vertices and a topology
  bool isMesh( void ) const { return hasVertices() && hasTriangles(); }

  // no check on index cnt%3==0
  bool hasTriangles( void ) const { return (attributes.size() != 0) && (attributes[0].get()->getIndexCnt() > 0); }
  // no check on attr[0] type
  bool hasVertices( void ) const { return (attributes.size() != 0 ) && (attributes[0].get()->getDataCnt() > 0); }

  // return the number of triangles
  inline size_t getTriangleCount( void ) const { return attributes.size()==0 ? 0 : (attributes[0].get()->getIndexCnt()); }

  // return the number of vertices
  inline size_t getPositionCount( void ) const { return attributes.size()==0 ? 0 : (attributes[0].get()->getDataCnt()); }

};


// unify a mesh so that vertices and attibutes are unique
inline bool unify(const Model& input, Model& output, bool positionsOnly = false) {
    
    output = Model(input); // deep copy could be optimized

    typedef std::vector<float> floatVector;

    struct VectorHasherfloat {
        std::size_t operator()(const std::vector<float> vec) const {
            auto hash = std::hash<float>()(vec[0]);
            for (int i = 1; i < vec.size(); i++) {
                hash ^= std::hash<float>()(vec[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

    std::vector<bool> processedAttr(input.attributes.size(), false);
    for (size_t attrIdx = 0; attrIdx < input.attributes.size(); ++attrIdx)
    {
        if (processedAttr[attrIdx]) continue;
        processedAttr[attrIdx] = true;
        const auto& iAttr = *input.attributes[attrIdx].get();
        auto& oAttr = *output.attributes[attrIdx].get();
        if (positionsOnly && !(iAttr.getType()==AttrType::POSITION)) continue;
        if (iAttr.getDomain() != AttrDomain::PER_VERTEX) continue;

        // unification logic of attributes marked as having a reference index to be validated and eased through a decicated api

        std::vector<size_t> combinedAttributes;

        for (size_t depIdx = 0; depIdx < input.attributes.size(); ++depIdx)
        {
            const auto& dAttr = *input.attributes[depIdx].get();
            if (dAttr.getIndexRef() == attrIdx)
            {
                // warning : a specific unification is performed when normals are using the same index as positions in the current release
                // would require dedicated api and explicit signalling for reverse unification in decoding
                // TODO we should make sure there that combined dependent have no indices
                // there different from v80 - api needed to combine when not using reverseUnification - in TMMv8.0 normals are always combined with positions
                if ((dAttr.getType() == AttrType::NORMAL) && (1 || positionsOnly)) // 1|| added to replicate v8.0
                //if ((dAttr.getType() == AttrType::NORMAL) && (positionsOnly)) // replicates v8.0 when reverseUnification=1 only - caution then not same model encoded depending on options
                {
                    combinedAttributes.push_back(depIdx);
                    processedAttr[depIdx] = true;
                }
                // TODO non combined dependents should have there index set to the original reference
            }
        }


        // NOTE current implementation limited to float attribute values
        const auto& ivertices = *iAttr.getData<float>();
        auto& overtices = *oAttr.getData<float>();
        const size_t dim = iAttr.getDim();

        std::vector<int32_t>        mapping(iAttr.getDataCnt());
        int32_t pointCounter = 0;

        std::unordered_map<floatVector, int32_t, VectorHasherfloat> uniquePoints(iAttr.getDataCnt());
        
        overtices.clear();
        overtices.reserve(ivertices.size());
        for (auto& combIdx : combinedAttributes)
        {
            const auto& coAttr = *output.attributes[combIdx].get();
            auto& covertices = *coAttr.getData<float>();
            const size_t cdim = coAttr.getDim();
            covertices.clear();
            covertices.reserve(cdim*ivertices.size()/dim); 
        }
        for (int32_t index = 0; index < ivertices.size()/dim; ++index) {
            floatVector pt;
            for (auto k = 0; k < dim; ++k)
                pt.push_back(ivertices[index * dim + k]);

            // to be rewritten for perfs
            for (auto& combIdx : combinedAttributes)
            { 
                const auto& cAttr = *input.attributes[combIdx].get();
                const auto& cvertices = *cAttr.getData<float>();
                const size_t cdim = cAttr.getDim();
                for (auto k = 0; k < cdim; ++k)
                    pt.push_back(cvertices[index * cdim + k]);
            }
            const auto  it = uniquePoints.find(pt);
            if (it == uniquePoints.end()) {
                int i = 0;
                for (auto k=0; k<dim;++k)
                    overtices.push_back(pt[i++]);
                for (auto& combIdx : combinedAttributes)
                {
                    const auto& coAttr = *output.attributes[combIdx].get();
                    auto& covertices = *coAttr.getData<float>();
                    const size_t cdim = coAttr.getDim();
                    for (auto k = 0; k < cdim; ++k)
                        covertices.push_back(pt[i++]);
                }
                uniquePoints[pt] = pointCounter;
                mapping[index] = pointCounter;
                ++pointCounter;
            }
            else {
                mapping[index] = it->second;
            }
        }
        overtices.resize(dim * pointCounter);
        oAttr.setDataCnt(pointCounter);
        for (auto& combIdx : combinedAttributes)
        {
            auto& coAttr = *output.attributes[combIdx].get();
            auto& covertices = *coAttr.getData<float>();
            covertices.resize(coAttr.getDim()* pointCounter);
            coAttr.setDataCnt(pointCounter);
        }
        bool reindex = pointCounter < iAttr.getDataCnt();
        if (iAttr.getIndexRef() != -1) // systematic copy to avoid pb with resize below - to be rewrtitten
        {
            // caution very dangerous - requires & to properly handle memory there
            oAttr.setIndices(*(*input.attributes[iAttr.getIndexRef()].get()).getIndices<int32_t>());
        }
        if (reindex)
        {
            oAttr.setDataCnt(pointCounter);
            const auto& itriangles = (iAttr.getIndexRef() == -1) ? *iAttr.getIndices<int32_t>() : *input.attributes[iAttr.getIndexRef()].get()->getIndices<int32_t>();
            auto& otriangles = *oAttr.getIndices<int32_t>();
            otriangles.resize(itriangles.size());
            for (auto& combIdx : combinedAttributes)
            {
                const auto& coAttr = *output.attributes[combIdx].get();
                auto cotriangles = coAttr.getIndices<int32_t>();
                if (cotriangles) cotriangles->resize(itriangles.size());
            }
            for (int32_t index = 0; index < otriangles.size(); ++index) {
                otriangles[index] = mapping[otriangles[index]];
                // to rewrite .. either copy the resulting vector in the end of make the code use the reference
                for (auto& combIdx : combinedAttributes)
                {
                    const auto& coAttr = *output.attributes[combIdx].get();
                    auto cotriangles = coAttr.getIndices<int32_t>();
                    if (cotriangles) (*cotriangles)[index] = mapping[otriangles[index]];
                }
            }
        }
        oAttr.setIndexRef(-1);

    }
    return true;
}



//============================================================================
// Fixed point inverse square root
namespace rsqrt {
    static const uint64_t k3timesR[96] = {
      3196059648, 3145728000, 3107979264, 3057647616, 3019898880, 2969567232,
      2931818496, 2894069760, 2868903936, 2831155200, 2793406464, 2768240640,
      2730491904, 2705326080, 2667577344, 2642411520, 2617245696, 2592079872,
      2566914048, 2541748224, 2516582400, 2491416576, 2466250752, 2441084928,
      2428502016, 2403336192, 2378170368, 2365587456, 2340421632, 2327838720,
      2302672896, 2290089984, 2264924160, 2252341248, 2239758336, 2214592512,
      2202009600, 2189426688, 2164260864, 2151677952, 2139095040, 2126512128,
      2113929216, 2101346304, 2088763392, 2076180480, 2051014656, 2038431744,
      2025848832, 2013265920, 2000683008, 2000683008, 1988100096, 1962934272,
      1962934272, 1950351360, 1937768448, 1925185536, 1912602624, 1900019712,
      1900019712, 1887436800, 1874853888, 1862270976, 1849688064, 1849688064,
      1837105152, 1824522240, 1811939328, 1811939328, 1799356416, 1786773504,
      1786773504, 1774190592, 1761607680, 1761607680, 1749024768, 1736441856,
      1736441856, 1723858944, 1723858944, 1711276032, 1698693120, 1698693120,
      1686110208, 1686110208, 1673527296, 1660944384, 1660944384, 1648361472,
      1648361472, 1635778560, 1635778560, 1623195648, 1623195648, 1610612736 };

    static const uint64_t kRcubed[96] = {
      4195081216, 3999986688, 3857709056, 3673323520, 3538940928, 3364924416,
      3238224896, 3114735616, 3034196992, 2915990528, 2800922624, 2725880832,
      2615890944, 2544223232, 2439185408, 2370818048, 2303728640, 2237913088,
      2173355008, 2110061568, 2048008192, 1987165184, 1927563264, 1869150208,
      1840392192, 1783783424, 1728321536, 1701024768, 1647311872, 1620883456,
      1568898048, 1543306240, 1492993024, 1468236800, 1443762176, 1395656704,
      1372007424, 1348605952, 1302626304, 1280060416, 1257736192, 1235650560,
      1213861888, 1192294400, 1171008512, 1149979648, 1108673536, 1088379904,
      1068352512, 1048567808, 1029031936, 1029036032, 1009729536, 971888640,
      971882496,  953319424,  934993920,  916897792,  899011584,  881389568,
      881392640,  864009216,  846846976,  829900800,  813182976,  813201408,
      796721152,  780459008,  764412928,  764417024,  748601344,  732995584,
      733017088,  717624320,  702468096,  702466048,  687520768,  672786432,
      672787456,  658258944,  658256896,  643947520,  629854208,  629862400,
      615976960,  615952384,  602276864,  588779520,  588804096,  575512576,
      575526912,  562433024,  562439168,  549556224,  549564416,  536876032 };
}  // namespace rsqrt

inline uint64_t irsqrt(uint64_t a64)
{
    using namespace rsqrt;

    if (!a64)
        return 0;

    int shift = -3;
    while (a64 & 0xffffffff00000000) {
        a64 >>= 2;
        shift--;
    }

    uint32_t a = a64;
    while (!(a & 0xc0000000)) {
        a <<= 2;
        shift++;
    }

    // initial approximation and first fixed point iteration:
    //   r' = (3r-ar^3)/2 (divide by 2 will by handled by shifts)
    int idx = (a >> 25) - 32;
    uint64_t r = k3timesR[idx] - ((kRcubed[idx] * a) >> 32);

    // second fixed point iteration
    uint64_t ar = (r * a) >> 32;
    uint64_t s = 0x30000000 - ((r * ar) >> 32);
    r = (r * s) >> 32;

    // denormalize
    if (shift > 0)
        return r << shift;
    else
        return r >> -shift;
}

inline uint32_t isqrt(uint64_t x)
{
    if (x <= (uint64_t(1) << 46))
        return 1 + ((x * irsqrt(x)) >> 40);
    else {
        uint64_t x0 = (x + 65536) >> 16;
        return 1 + ((x0 * irsqrt(x0)) >> 32);
    }
}

//---------------------------------------------------------------------------

inline uint32_t ceilpow2(uint32_t x)
{
    x--;
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >> 16);
    return x + 1;
}

// Population count -- return the number of bits set in @x.
//
inline int popcnt(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    return ((x + (x >> 4) & 0xF0F0F0Fu) * 0x1010101u) >> 24;
}

// Compute \left\floor \text{log}_2(x) \right\floor.
// NB: ilog2(0) = -1.
inline int ilog2(uint32_t x)
{
    x = ceilpow2(x + 1) - 1;
    return popcnt(x) - 1;
}

template<unsigned NIter = 3>
inline int64_t recipApprox(int64_t b, int32_t& log2Scale)
{
  int log2ScaleOffset = 0;
  int32_t log2bPlusOne = ilog2(uint64_t(b)) + 1;

  if (log2bPlusOne > 31) {
    b >>= log2bPlusOne - 31;
    log2ScaleOffset -= log2bPlusOne - 31;
  }

  if (log2bPlusOne < 31) {
    b <<= 31 - log2bPlusOne;
    log2ScaleOffset += 31 - log2bPlusOne;
  }

  // Initial approximation: 48/17 - 32/17 * b with 28 bits decimal prec
  int64_t bRecip = ((0x2d2d2d2dLL << 31) - 0x1e1e1e1eLL * b) >> 28;
  for (unsigned i = 0; i < NIter; ++i)
    bRecip += bRecip * ((1LL << 31) - (b * bRecip >> 31)) >> 31;

  log2Scale = (31 << 1) - log2ScaleOffset;
  return bRecip;
}
//---------------------------------------------------------------------------

}  // namespace mm

#endif