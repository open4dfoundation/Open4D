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
// loadObj/saveObj methods based on original code from Owlii

// remove warning when using sprintf on MSVC
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <time.h>
#include <cmath>
// ply loader
// #define TINYPLY_IMPLEMENTATION
// will use the one from VMESH or ebEncode or ebDecode
#include "tinyply.h"
// mathematics
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include "ebIO.h"
#include "ebChrono.h"

using namespace eb;

///////////////////////////
// Private methods

bool IO::loadModel( std::string filename, Model& output ) {
  bool success = true;

  // sanity check
  if ( filename.size() < 5 ) {
    std::cout << "Error, invalid mesh file name " << filename << std::endl;
    return false;
  }

  // get extension
  std::string ext = filename.substr( filename.size() - 3, 3 );
  std::for_each( ext.begin(), ext.end(), []( char& c ) { c = ::tolower( c ); } );

  // do the job
  if ( ext == "obj" ) {
    std::cout << "Loading file: " << filename << std::endl;
    auto t1 = clock();
    success = IO::loadObj( filename, output );
    if ( success ) {
      auto t2 = clock();
      std::cout << "Time on loading: " << ( (float)( t2 - t1 ) ) / CLOCKS_PER_SEC << " sec." << std::endl;
    }
  }
  else if (ext == "vmb") {
      std::cout << "Loading file: " << filename << std::endl;
      auto t1 = clock();
      success = IO::loadVmb(filename, output);
      if (success) {
          auto t2 = clock();
          std::cout << "Time on loading: " << ((float)(t2 - t1)) / CLOCKS_PER_SEC << " sec." << std::endl;
      }
  } else {
    std::cout << "Error, invalid mesh file extension (not in obj, vmb)" << std::endl;
    return false;
  }

  if ( success ) {
    // print stats
    std::cout << "Input model: " << filename << std::endl;
    for (const auto& attrPtr : output.attributes)
    {
        const auto& attr = *attrPtr.get();
        std::cout << " " << attr.getType() << " - Values : " << attr.getDataCnt() << " , Faces : " << attr.getIndexCnt() << " ( " << attr.getDim() << ", " << attr.getDataType() << " ) " << attr.getDomain() << std::endl;
        /* when the attribute domain is PER_FACE and Faces is reported as 0, the value count should be equal to the face count of the primary attribute */
        /* when the reference index is not -1, Faces is reported as 0 as a separate set of indices is hot defined for the attribute */
    }
  }
  else
  {
      std::cout << "Error loading model: " << filename << std::endl;
  }

  return success;
}

bool IO::saveModel( std::string filename, const Model& input, const bool forceAscii ) {
  // sanity check
  if ( filename.size() < 5 ) {
    std::cout << "Error, invalid mesh file name " << filename << std::endl;
    return false;
  }

  // check output file extension
  std::string out_ext = filename.substr( filename.size() - 3, 3 );
  std::for_each( out_ext.begin(), out_ext.end(), []( char& c ) { c = ::tolower( c ); } );

  // write output
  if ( out_ext == "obj" ) {
    std::cout << "Saving file: " << filename << std::endl;
    auto t1  = clock();
    auto err = IO::saveObj( filename, input );
    if ( !err ) {
      auto t2 = clock();
      std::cout << "Time on saving: " << ( (float)( t2 - t1 ) ) / CLOCKS_PER_SEC << " sec." << std::endl;
    }
    return err;
  }
  else if (out_ext == "vmb") {
      std::cout << "Saving file: " << filename << std::endl;
      auto t1 = clock();
      auto err = IO::saveVmb(filename, input);
      if (!err) {
          auto t2 = clock();
          std::cout << "Time on saving: " << ((float)(t2 - t1)) / CLOCKS_PER_SEC << " sec." << std::endl;
      }
      return err;
  } else {
    std::cout << "Error: invalid mesh file extension (not in obj, vmb)" << std::endl;
    return false;
  }

  // success
  return true;
}

class iBuffer {

public:
    std::vector<char> buffer; // the bitstream
    char* head = 0;           // current read position
    char* end = 0;            // end of buffer
    char* ls = 0;             // start of current line
    int lc = 1;               // linecount

    inline bool load(std::string filename) {
        // if loader is reused need to reset
        buffer.clear();
        lc=1; 

        FILE* fp = fopen(filename.c_str(), "rb"); // open input file in binary mode
        if (!fp) {
            std::cerr << "Error: can't open file " << filename << std::endl;
            return false;
        }

        if (fseek(fp, SEEK_SET, SEEK_END) != 0) {
            std::cerr << "Error: can't seek to end of file to get size " << filename << std::endl;
            fclose(fp);
            return false;
        }
        const size_t fileSize = static_cast<size_t>(ftell(fp));
        if (fileSize == 0) {
            std::cerr << "Error: empty file " << filename << std::endl;
            fclose(fp);
            return false;
        }
        // load the data into memory
        buffer.resize(fileSize + 1); // forcing buffer ending with 0x00 to fix calls to int/float parsing (strtol ..)
        rewind(fp);
        auto size = 0;
        if ((size = fread(buffer.data(), 1, fileSize, fp)) != fileSize) {
            if (feof(fp)) {
                std::cerr << "Error: can't load file into memory for parsing, unexpected end of file " << filename << std::endl;
            }
            else if (ferror(fp)) {
                std::cerr << "Error: can't load file into memory for parsing, error while reading " << filename << std::endl;
            } // else shall never occur ?
            fclose(fp);
            return false;
        }
        fclose(fp);

        ls = head = &buffer[0];
        end = buffer.data() + buffer.size();

        return true;
    }

    // reads a char, current head must be valid
    // returns new head, 
    inline char* getChar(char& c) {
        c = *head;
        return ++head;
    }
    // moves read head nb char back, current head can be = end
    // no sanity check, head-n shall not be less than buffer start.
    // returns new head, 
    inline char* readBack(int n) {
        return (head = head - n);
    }
    // reads a line and append to s, current head must be valid
    // returns new head, 
    // not efficient
    inline char* getLine(std::string& s) {
        char c;
        while (getChar(c) < end && c != '\n' && c != '\r') {
            s.push_back(c);
        } 
        // put back the last getChar, since it was a special char
        readBack(1);
        // eat eventual additional end of line special characters
        if (head != end) {
            while (getChar(c) < end && (c == '\n' || c == '\r')) { // we skip
                if (c == '\n') {++lc; ls = head; }
            }
            // put back the last getChar, since it was not a special char
            readBack(1);
        }
        return head;
    }
    // reads a word (space separator) and append to s, current head must be valid
    // returns new head,
    inline char* getWord(std::string& s) {
        char c;
        while (getChar(c) != end && c != ' ' && c != '\n' && c != '\r') {
            s.push_back(c);
        }  // \n or \r was read but not pushed
        // eat eventual additional end of line special characters
        if (head != end) {
            while (getChar(c) != end && (c == '\n' || c == '\r')) {  // we skip
                if (c == '\n') ++lc;
            }
            // put back the last getChar, since it was not a special char
            readBack(1);
        }
        return head;
    }
    // reads all successive spaces,\n and \r if any, current head must be valid
    // this can skip several lines in one call
    // returns new head, 
    inline char* skipSpaces() {
        char c;
        while (getChar(c) < end && isspace(c)) {  // we skip
            if (c == '\n') {++lc; ls = head; }
        }
        // last char is not a apsce or special, we readback
        return readBack(1);
    }
    // skip line, eat chars until '\n' included or end of buffer is reached, current head must be valid
    // returns new head, 
    inline char* skipLine() {
        char c;
        while (head < end && getChar(c) < end && c != '\n' && c != '\r') {
            // we skip
        } // \n or \r was read but not pushed
        // put back the last getChar, since it was not special char
        readBack(1);
        // eat eventual additional end of line special characters
        while (head < end && getChar(c) < end && (c == '\n' || c == '\r')) {
            // we skip
            if (c == '\n') {++lc; ls = head; }// count a new line
        }
        // put back the last getChar, since it was not a special char
        readBack(1);
        return head;
    }

    // no error check
    inline double getDouble(void) {
        char* endptr = 0;
        const double val = strtod(head, &endptr);
        const auto success = (endptr != 0) && (endptr != head);
        if (success)
            head = endptr;
        return val;
    }

    // with error check
    inline bool getDouble(double& val) {
        char* endptr = 0;
        val = strtod(head, &endptr);
        const auto success = (endptr != 0) && (endptr != head);
        if (success)
            head = endptr;
        return success;
    }

    // no error check
    inline long int getInteger(const int base = 10) {
        char* endptr = 0;
        auto val = strtol(head, &endptr, base);
        const auto success = (endptr != 0) && (endptr != head);
        if (success)
            head = endptr;
        return val;
    }

    // with error check
    inline bool getInteger(long int& val, const int base = 10) {
        char* endptr = 0;
        val = strtol(head, &endptr, base);
        const auto success = (endptr != 0) && (endptr != head);
        if (success)
            head = endptr;
        return success;
    }
};

bool IO::loadObj(std::string filename, Model& output) {

    // open file and load in memory
    iBuffer bs; // the bitstream
    if (!bs.load(filename))
        return false;
    //
    char c = 0;         // current character
    std::string token;  // buffer to build some tokens of more than one char
    int nIdxCount = 0;  // number of normal indices read
    int uvIdxCount = 0; // number of uv indices read
    int nbTessPol = 0;  // number of polygons/quads tesselated
    int nbTessAdd = 0;  // number of triangles added by tesselation
    int matIdx = 0;     // the material index, 0 by default (white)
    bool useFaceId = false;
    // consume the first character
    if ((bs.skipSpaces() == bs.end) || (bs.getChar(c) == bs.end))
        return false;
    // consume lines

    // assuming float type
    std::vector<float>       vertices;     // vertex positions (x,y,z)
    std::vector<float>       uvcoords;     // vertex uv coordinates (u,v)
    std::vector<float>       normals;      // vertex normals (x,y,z)
    std::vector<float>       colors;       // vertex colors (r,g,b)
    // assuming int type
    std::vector<int>         triangles;    // triangle position indices
    std::vector<int>         trianglesuv;  // triangle uv indices
    std::vector<int>         trianglesn;   // triangle normal indices
    std::vector<int>         faceid;       // per face material id
    //
    std::vector<std::string> materialNames;

    while (bs.head < bs.end) {
        // at this point either line necessarly have a first 
        // character and it is not a space or special
        if (c == 'v') {
            char c2 = 0;
            // consume the second character
            if (bs.getChar(c2) == bs.end)
                return false;

            if (c2 == ' ') {
                // parse the position
                for (int i = 0; i < 3; i++) {
                    double value = 0;
                    if (!bs.getDouble(value)) {
                        std::cerr << "Error: line " << bs.lc << " expected floating point value" << std::endl;
                    }
                    // may push zero to be "robust"
                    vertices.push_back(value);
                }
                // parse the color if any (re map 0.0-1.0 to 0-255 internal color format)
                double value = 0;
                if (bs.getDouble(value)) {
                    colors.push_back(std::roundf(value * 255));
                    for (int i = 0; i < 2; i++) {
                        value = 0;
                        if (!bs.getDouble(value)) {
                            std::cerr << "Error: line " << bs.lc << " expected floating point value" << std::endl;
                        }
                        // may push zero to be "robust"
                        colors.push_back( std::roundf( value * 255 ) );
                    }
                }
            }
            else if (c2 == 'n') {
                // parse the normal
                for (int i = 0; i < 3; i++) {
                    double value = 0;
                    if (!bs.getDouble(value)) {
                        std::cerr << "Error: line " << bs.lc << " expected floating point value" << std::endl;
                    }
                    // may push zero to be "robust"
                    normals.push_back(value);
                }
            }
            else if (c2 == 't') {
                // parse the texture coordinate
                for (int i = 0; i < 2; i++) {
                    double value = 0;
                    if (!bs.getDouble(value)) {
                        std::cerr << "Error: line " << bs.lc << " expected floating point value" << std::endl;
                    }
                    // may push zero to be "robust"
                    uvcoords.push_back(value);
                }
            }
        }
        else if (c == 'f') {
            // max vertices for a polygon (the rest will be skiped)
            const auto maxVertices = 8;
            std::array<int32_t, 3> indices[maxVertices];
            int numValidIndices = 0;
            for (int i = 0; i < maxVertices; i++) { // up to 8 vertices
                bool valid = true;
                // parses one vertex pos/[tex][/norm]
                // ugly if cascade sorry :-(
                // coudl be moved in a separate func for better 
                // legibility and les if/else (through early returns)
                long int value = 0;
                if (bs.getInteger(value)) {
                    indices[i][0] = value; // pos index
                    indices[i][1] = indices[i][2] = 1;
                    // some more ?
                    char f2;
                    if (bs.head < bs.end) {
                        bs.getChar(f2);
                        if (f2 == '/') {
                            if (bs.head < bs.end) {
                                if (bs.getInteger(value)) {
                                    ++uvIdxCount;
                                    indices[i][1] = value; // UV index (optional)
                                }
                                if (bs.head < bs.end) {
                                    char f3;
                                    bs.getChar(f3);
                                    if (f3 == '/') {
                                        if (bs.head < bs.end) {
                                            if (bs.getInteger(value)) {
                                                ++nIdxCount;
                                                indices[i][2] = value; // normal index (mandatory)
                                            }
                                            else {
                                                valid = false;
                                            }
                                        }
                                        else {
                                            valid = false;
                                        }
                                    }
                                    else {
                                        bs.readBack(1);
                                    }
                                }
                            }
                            else {
                                valid = false;
                            }
                        }
                        else {
                            bs.readBack(1);
                        }
                    } // else no more
                }
                else {
                    valid = false;
                }

                //
                if (valid) {
                    ++numValidIndices;
                }
                else if (i < 3) {
                    std::cerr << "Error: line " << bs.lc << " invalid vertex indices, skipping face" << std::endl;
                    std::cerr << "Error: " << std::string( bs.ls, std::min(bs.head, bs.end) ) << std::endl;
                }
                else { // we stop it is not valid but this is allowed
                    break;
                }
            }
            if (numValidIndices >= 3) { // skip if error
                // Process the first triangle.
                for (int i = 0; i < 3; ++i) {
                    triangles.push_back(indices[i][0] - 1);
                    trianglesuv.push_back(indices[i][1] - 1);
                    trianglesn.push_back(indices[i][2] - 1);
                    // no normal index table for the time being
                }
                faceid.push_back(matIdx);
                // triangulate eventual quad or polygon from first vertex
                // tri 0 -> 0 1 2 (the main one already done)
                // tri 1 -> 0 2 3 si=2
                // tri 2 -> 0 3 4 si=3 and so on
                // Iterate over start index
                for (int si = 2; si < numValidIndices - 1; si++) {
                    triangles.push_back(indices[0][0] - 1);
                    trianglesuv.push_back(indices[0][1] - 1);
                    trianglesn.push_back(indices[0][2] - 1);
                    // push the two other indices
                    for (int ci = 0; ci < 2; ci++) {
                        triangles.push_back(indices[si + ci][0] - 1);
                        trianglesuv.push_back(indices[si + ci][1] - 1);
                        trianglesn.push_back(indices[si + ci][2] - 1);
                    }
                    faceid.push_back(matIdx);
                }
                nbTessPol += (int)(numValidIndices > 3);
                nbTessAdd += numValidIndices - 3;
                useFaceId |= matIdx > 0;
            }
        }
        else if (c == 'm') { // material lib (key is 'mtllib', but no other starts with 'm')
            token = "m";
            if (bs.getWord(token) != bs.end && token == "mtllib") {
                std::string materialLibFilename;
                if (bs.skipSpaces() != bs.end) {
                    bs.getLine(materialLibFilename);
                }
                // if there is already a material in the header we push 
                // a line break before adding an additional material
                if (output.header.size() != 0) output.header += '\n';
                output.header += "mtllib ";
                output.header += materialLibFilename;
                bs.readBack(1); // push back the end of line symbol so generic skipLine will work
            }
        }
        else if (c == 'u') {
            token = "u";
            if (bs.getWord(token) != bs.end && token == "usemtl") {
                // the materialIndex may be updated at this point
                std::string materialName;
                if (bs.skipSpaces() != bs.end) { bs.getLine(materialName); }
                bool newmtl = true;
                for (int i = 0; i < materialNames.size(); i++) {
                    if (materialName == materialNames[i]) {
                        matIdx = i+1;
                        newmtl = false;
                        break;
                    }
                }
                if (newmtl) {
                    materialNames.push_back(materialName);
                    matIdx = materialNames.size();
                }
                bs.readBack(1);  // push back the end of line symbol so generic skipLine will work
            }
        }

        // purge eventual end of line
        // This silent skip unsupported tags or errors
        if ( bs.skipLine() < bs.end ) {
            // purge useless chars and then consume the first character of next non empty line
            if ( bs.skipSpaces() < bs.end ) { 
                bs.getChar( c ); 
            }
        }
    }

    if (!useFaceId) {
        faceid.clear();
    }

    if (uvIdxCount == 0) {
        // did not find any uv indices in the file
        // if partial indices we keep the table (set to 0 for faces with no idx in the file)
        trianglesuv.clear();
    }

    if (nIdxCount == 0) {
        // same as above for uvs
        trianglesn.clear();
    }
    else {
        std::cout << "Warning: obj read, normals with separate index table support is experimental." << std::endl;
    }
    if (nbTessPol != 0) {
        std::cout << "Tesselated " << nbTessPol << " polygons/quads introducing " << nbTessAdd << " triangles" << std::endl;
    }

    if (vertices.size())
    {
        output.attributes.emplace_back(
            std::make_shared<MeshAttribute>(AttrType::POSITION, DataType::FLOAT32, 3, AttrDomain::PER_VERTEX, vertices, triangles)
        );
        if (uvcoords.size())
        {
            if (trianglesuv.size())
            {
                output.attributes.emplace_back(
                    std::make_shared<MeshAttribute>(AttrType::TEXCOORD, DataType::FLOAT32, 2, AttrDomain::PER_VERTEX, uvcoords, trianglesuv)
                );
            }
            else
            {
                output.attributes.emplace_back(
                    std::make_shared<MeshAttribute>(AttrType::TEXCOORD, DataType::FLOAT32, 2, AttrDomain::PER_VERTEX, uvcoords, 0)
                );
            }
        }
        if (normals.size())
        {
            if (trianglesn.size())
            {
                output.attributes.emplace_back(
                    std::make_shared<MeshAttribute>(AttrType::NORMAL, DataType::FLOAT32, 3, AttrDomain::PER_VERTEX, normals, trianglesn)
                );
            }
            else
            {
                output.attributes.emplace_back(
                    std::make_shared<MeshAttribute>(AttrType::NORMAL, DataType::FLOAT32, 3, AttrDomain::PER_VERTEX, normals, 0)
                );
            }
        }
        if (colors.size() == vertices.size()) // restricted to same index as position
        {
            output.attributes.emplace_back(
                std::make_shared<MeshAttribute>(AttrType::COLOR, DataType::FLOAT32, 3, AttrDomain::PER_VERTEX, colors, 0)
            );
        }
        if (faceid.size())
        {
            output.attributes.emplace_back(
                std::make_shared<MeshAttribute>(AttrType::MATERIAL_ID, DataType::INT32, 1, AttrDomain::PER_FACE, faceid)
            );
        }
    }
    return true;
}

namespace eb
{
// Buffer used for encoding float/int numbers.
    char num_buffer_[20];

    bool Encode(const void* data, size_t data_size, std::vector<char>& bitstream) {
        const uint8_t* src_data = reinterpret_cast<const uint8_t*>(data);
        bitstream.insert(bitstream.end(), src_data, src_data + data_size);
        return true;
    }

    void EncodeFloat(float val, std::vector<char>& bitstream) {
        snprintf(num_buffer_,
            sizeof(num_buffer_),
            "%.9g",
            val);  //%.9g keeps backward compat with previous printer
        Encode(num_buffer_, strlen(num_buffer_), bitstream);
    }

    void EncodeFloatList(float* vals, int num_vals, std::vector<char>& bitstream) {
        for (int i = 0; i < num_vals; ++i) {
            if (i > 0) { Encode(" ", 1, bitstream); }
            EncodeFloat(vals[i], bitstream);
        }
    }

    void EncodeInt(int32_t val, std::vector<char>& bitstream) {
        snprintf(num_buffer_, sizeof(num_buffer_), "%d", val);
        Encode(num_buffer_, strlen(num_buffer_), bitstream);
    }
}
bool IO::saveObj(std::string filename, const Model& input) {
    FILE* fp = fopen(filename.c_str(), "w+");
    if (!fp) {
        std::cerr << "Error: can't open file " << filename << std::endl;
        return false;
    }

    std::vector<char> bs;  // the bitstream

    std::vector<float> emptyFloat;
    std::vector<int32_t> emptyInt;

    // assuming float type
    std::vector<float>       *vertices    = nullptr;  // vertex positions (x,y,z)
    std::vector<float>       *uvcoords    = nullptr;  // vertex uv coordinates (u,v)
    std::vector<float>       *normals     = nullptr;  // vertex normals (x,y,z)
    // per vertex colors are not natively supported in the obj file format
    std::vector<float>       *colors      = nullptr;  // vertex colors (r,g,b)
    //assuming int32 types
    std::vector<int32_t>     *triangles   = nullptr;  // triangle position indices
    std::vector<int32_t>     *trianglesuv = nullptr;  // triangle uv indices
    std::vector<int32_t>     *trianglesn  = nullptr;  // triangle normal indices
    std::vector<int32_t>     *faceid      = nullptr;  // per face material id

    // current implementation assumes attributes to be per vertex, and attributes values to be float
    for (const auto& attrPtr : input.attributes)
    {
        const MeshAttribute* attr = attrPtr.get();
        if ((attr->getType() == AttrType::POSITION) && (attr->getDataType() == DataType::FLOAT32))
        {
            if (!vertices) {
                vertices = attr->getData<float>();
                triangles = attr->getIndices<int32_t>();
            }
            else
                std::cout << "WARNING : multiple position attributes not supported in this format, keeping first" << std::endl;
        }
        if ((attr->getType() == AttrType::TEXCOORD) && (attr->getDataType() == DataType::FLOAT32))
        {
            if (!uvcoords) {
                uvcoords = attr->getData<float>();
                if (attr->getIndexCnt())
                    trianglesuv = attr->getIndices<int32_t>();
            }
            else
                std::cout << "WARNING : multiple texture coordinates attributes not supported in this format, keeping first" << std::endl;
        }
        // per vertex colors are not natively supported in the obj file format
        if (!colors && (attr->getType() == AttrType::COLOR) && (attr->getDataType() == DataType::FLOAT32))
        {
            colors = attr->getData<float>();
            // implicit index assumed in extended obj format including colors
        }
        if (!normals && (attr->getType() == AttrType::NORMAL) && (attr->getDataType() == DataType::FLOAT32))
        {
            if (!normals) {
                normals = attr->getData<float>();
                if (attr->getIndexCnt())
                    trianglesn = attr->getIndices<int32_t>();
            }
            else
                std::cout << "WARNING : multiple normal attributes not supported in this format, keeping first" << std::endl;
        }
        if ((attr->getType() == AttrType::MATERIAL_ID) && (attr->getDataType() == DataType::INT32))
        {
            if (!faceid)
                faceid = attr->getData<int32_t>(); // implicit index assumed
            else
                std::cerr << "WARNING : multiple material identification attributes not supported in this format, keeping first" << std::endl;
        }
    }

    if (!vertices) vertices = &emptyFloat;  // vertex positions (x,y,z)
    if (!uvcoords) uvcoords = &emptyFloat;  // vertex uv coordinates (u,v)
    if (!normals) normals = &emptyFloat;    // vertex normals (x,y,z)
    if (!colors) colors = &emptyFloat;      // vertex colors (r,g,b)
    if (!triangles) triangles = &emptyInt;  // triangle position indices
    if (!trianglesuv) trianglesuv = &emptyInt;  // triangle uv indices
    if (!trianglesn) trianglesn = &emptyInt;    // triangle normal indices
    if (!faceid) faceid = &emptyInt;            // per face id

    printf("_saveObj %-40s: V = %zu Vc = %zu N = %zu UV = %zu F = %zu Fuv = %zu Fnor = %zu Fid = %zu\n",
      filename.c_str(),
      vertices->size() / 3,
      colors->size() / 3,
      normals->size() / 3,
      uvcoords->size() / 2,
      triangles->size() / 3,
      trianglesuv->size() / 3,
      trianglesn->size() / 3,
      faceid->size());
    fflush(stdout);

    Encode(input.header.data(), input.header.size(), bs);
    Encode("\n", 1, bs);

    for (int i = 0; i < vertices->size() / 3; i++) {
        Encode("v", 1, bs);
        for (auto c = 0; c < 3; ++c) {
            Encode(" ", 1, bs);
            EncodeFloat((*vertices)[i * 3 + c], bs);
        }
        // some obj implementations do handle per vertex colors appeneded to positions
        if (colors->size() == vertices->size()) {
            for (auto c = 0; c < 3; ++c) {
                Encode(" ", 1, bs);
                EncodeFloat((*colors)[i * 3 + c], bs);
            }
        }
        Encode("\n", 1, bs);
    }
    for (int i = 0; i < normals->size() / 3; i++) {
        Encode("vn", 2, bs);
        for (auto c = 0; c < 3; ++c) {
            Encode(" ", 1, bs);
            EncodeFloat((*normals)[i * 3 + c], bs);
        }
        Encode("\n", 1, bs);
    }
    for (int i = 0; i < uvcoords->size() / 2; i++) {
        Encode("vt", 2, bs);
        for (auto c = 0; c < 2; ++c) {
            Encode(" ", 1, bs);
            EncodeFloat((*uvcoords)[i * 2 + c], bs);
        }
        Encode("\n", 1, bs);
    }
    const bool hasVertices = (vertices->size() != 0) && (vertices->size() % 3 == 0); 
    const bool hasUvCoords= uvcoords->size() != 0 && uvcoords->size() % 2 == 0 &&
            ((trianglesuv->size() != 0 && trianglesuv->size() % 3 == 0) ||
                (hasVertices && uvcoords->size() / 2 == vertices->size() / 3));
    const bool hasNormals = normals->size() != 0 && normals->size() % 3 == 0 &&
        ((trianglesn->size() != 0 && trianglesn->size() % 3 == 0) ||
            (hasVertices && normals->size() / 3 == vertices->size() / 3));
    // in the obj file format indices for positions/uv/normals are replicated even when identical 
    const bool implicituv = hasUvCoords && !(trianglesuv->size() == triangles->size());
    const bool  implicitn = hasNormals && !(trianglesn->size() == triangles->size());

    int refTextId = -1;
    if (hasUvCoords && faceid->empty()) {
        Encode("usemtl material0000\n", 20, bs); 
    }
    else if (!faceid->empty()) {
        refTextId = (*faceid)[0];
        // materials have been shuffled - default material to be included in the list if used
        // or faces reordered to have those using the default material listed before the first usemtl
        std::string materialName = "usemtl material" + std::to_string(refTextId) + "\n";
        Encode(materialName.data(), materialName.size(), bs);
    }

    for (int i = 0; i < triangles->size() / 3; i++) {
        if (refTextId >= 0) {
            if ((*faceid)[i] != refTextId) {
                refTextId = (*faceid)[i];
                std::string materialName = "usemtl material" + std::to_string(refTextId) + "\n";
                Encode(materialName.data(), materialName.size(), bs);
            }
        }
        // separate index for position, uv, normals
        Encode("f ", 2, bs);
        for (auto c = 0; c < 3; ++c) {
            if (c > 0) Encode(" ", 1, bs);
            EncodeInt((*triangles)[i * 3 + c] + 1, bs);
            if (hasUvCoords || hasNormals)
                Encode("/", 1, bs);
            if (hasUvCoords)
                EncodeInt((*(implicituv ? triangles : trianglesuv))[i * 3 + c] + 1, bs);
            if (hasNormals) {
                Encode("/", 1, bs);
                EncodeInt((*(implicitn ? triangles : trianglesn))[i * 3 + c] + 1, bs);
            }
        }
        Encode("\n", 1, bs);    
    }

    auto resSize = fwrite(bs.data(), sizeof(char), bs.size(), fp);
    fclose(fp);

    return resSize > 0;
}

template<tinyply::Type TiType, typename T>
void getVmb( std::vector<T>& dst, std::ifstream& fin, const int nbComp)
{
    if (dst.empty()) return;

    // little/big endian not adressed
    typedef std::conditional_t<TiType==tinyply::Type::FLOAT64, double,
              std::conditional_t< TiType==tinyply::Type::FLOAT32, float,
                 std::conditional_t< TiType==tinyply::Type::INT32, int32_t,
                   uint64_t > > > VmbType;

    std::vector<VmbType> tempArray;
    std::vector<VmbType>* srcArray;
    auto useTemp = (tinyply::PropertyTable[TiType].stride != sizeof(T));
    std::streamsize len = dst.size() * tinyply::PropertyTable[TiType].stride * nbComp;
    if (useTemp)
    {
        tempArray.resize(dst.size());
        char* destPtr = reinterpret_cast<char*>(tempArray.data());        
        fin.read(destPtr, len);
        srcArray = &tempArray;
    }
    
    else
    {
        char* destPtr = reinterpret_cast<char*>(dst.data());
        fin.read(destPtr, len);
        srcArray = reinterpret_cast<std::vector<VmbType>*>(&dst);
    }
    
    dst = std::vector<T>((*srcArray).begin(), (*srcArray).end());

}

bool IO::loadVmb(std::string filename, Model& output) {
    std::ifstream fin;
    // use a big 4MB buffer to accelerate reads
    char* buf = new char[4 * 1024 * 1024 + 1];
    fin.rdbuf()->pubsetbuf(buf, 4 * 1024 * 1024 + 1);
    fin.open(filename.c_str(), std::ifstream::binary | std::ifstream::in);
    if (!fin) {
        std::cerr << "Error: can't open file " << filename << std::endl;
        delete[] buf;
        return false;
    }
    bool binary = true;
    std::string line;
    std::getline(fin, line);
    {
        std::stringstream ss(line);
        std::string       name, value;
        ss >> name >> value;
        if (name != "#VMB" || ( value != "binary" && value != "ascii" ) ) {
            std::cerr << "ERROR: Read VMB " << filename << " can't read file format" << std::endl;
            exit(-1);
        }
        if (value == "ascii")
            binary = false;
    }

    tinyply::Type type=tinyply::Type::INVALID;

    // Read header
    std::string readChecksum;
    for (; getline(fin, line);) {
        const auto headerCount = fin.tellg();
        if (line.rfind("end_header", 0) == 0) break;
        else if (line.rfind("#", 0) == 0) continue;
        else {
            std::stringstream ss(line);
            AttrType attType;
            DataType attDataType;
            size_t   attDim;
            AttrDomain attDomain;
            size_t attValueCnt;
            int addIndexRef;
            size_t attIndexCnt;
            // no error check // what if attIndexCnt is not identical for pos and other attribs ?
            ss >> attType >> attDataType >> attDim >> attDomain >> attValueCnt >> addIndexRef >> attIndexCnt;

            std::shared_ptr<MeshAttribute> attrPtr = std::make_shared< MeshAttribute>(
                attType,
                attDataType,
                attDim,
                attDomain,
                attValueCnt,
                attIndexCnt,
                addIndexRef
                // create new here ?
            );
            auto& attrib = *attrPtr.get();
            switch (attDataType) {
            case DataType::INT32:
                attrib.setData(new std::vector<int32_t>(attValueCnt * attDim));
                break;
            case DataType::FLOAT32:
                attrib.setData(new std::vector<float>(attValueCnt * attDim));
                break;
            case DataType::FLOAT64:
                attrib.setData(new std::vector<double>(attValueCnt * attDim));
                break;
            }
            attrib.setIndices(new std::vector<int32_t>(attIndexCnt * 3)); // assumes tri only
            output.attributes.emplace_back(attrPtr);
        }
    }
    
    auto readAttribute = [&](auto& attrArray, const size_t dim) {
        if (!attrArray.empty())
        {
            if (binary)
            {
                using AttElemT = typename std::remove_reference<decltype(attrArray)>::type::value_type;
                const auto attrTypeSize = sizeof(AttElemT);
                fin.read(reinterpret_cast<char*>(attrArray.data()), attrArray.size() * attrTypeSize);
            }
            else
            {
                size_t count = 0;
                for (auto lidx = 0 ; lidx < attrArray.size()/dim; ++lidx)
                {
                    std::getline(fin, line); // to rewrite for speed
                    std::stringstream ss(line);
                    for (auto nc = 0; nc < dim; ++nc)
                    {
                        ss >> attrArray[count++];
                    }
                }
            }
        }
        };

    for (auto& attrPtr : output.attributes)
    {
        const auto& attr = *attrPtr.get();
        switch (attr.getDataType()) {
        case DataType::INT32:
            readAttribute(*attr.getData<int32_t>(), attr.getDim());
            break;
        case DataType::INT64:
            readAttribute(*attr.getData<int64_t>(), attr.getDim());
            break;
        case DataType::FLOAT32:
            readAttribute(*attr.getData<float>(), attr.getDim());
            break;
        case DataType::FLOAT64:
            readAttribute(*attr.getData<double>(), attr.getDim());
            break;
        }
        // assuming always int32 for now
        if (attr.getIndexRef() == -1) // check also attr.getIndexCnt() > 0 ?
            readAttribute(*attr.getIndices<int32_t>(), (attr.getDomain() == eb::AttrDomain::PER_FACE) ? 1 : 3); // triangles only, or per face
    }
    fin.close();
    delete[] buf;

    // current implementation could be extended to include material definitions, little/big endianness, and a checksum in the file header

    return true;
}

bool IO::saveVmb(std::string filename, const Model& input, const bool ascii) {
    std::ofstream fout;
    fout.open(filename.c_str(), std::ofstream::binary | std::ios::out);
    if (!fout) {
        std::cerr << "Error: can't open file " << filename << std::endl;
        return false;
    }

    // Write header
    fout << "#VMB " << (ascii ? "ascii" : "binary") << std::endl;

    for (auto& attr : input.attributes)
    {
        attr.get()->printHeader(fout);
    }
    // current implementation could be extended to include material definitions, little/big endianness, and a checksum in the file header
    fout << "end_header \n";

    // Write payload
    auto writeAttribute = [&](auto& attrArray, const size_t dim) {
        if (!attrArray.empty())
        {
            if (!ascii)
            {
                using AttElemT = typename std::remove_reference<decltype(attrArray)>::type::value_type;
                const auto attrTypeSize = sizeof(AttElemT);
                fout.write(reinterpret_cast<const char*>(attrArray.data()), attrArray.size() * attrTypeSize);
            }
            else
            {
                size_t count = 0;
                for (auto& elem : attrArray)
                {
                    auto estr = std::to_string(elem);
                    fout.write(estr.c_str(), estr.size());
                    count = (++count % dim);
                    fout.put((count == 0) ? '\n' : ' ');
                }
            }
        }
    };


    for (auto& attrPtr : input.attributes)
    {
        const auto& attr = *attrPtr.get();
        switch (attr.getDataType()) {
        case DataType::INT32:
            writeAttribute(*attr.getData<int32_t>(), attr.getDim());
            break;
        case DataType::INT64:
            writeAttribute(*attr.getData<int64_t>(), attr.getDim());
            break;
        case DataType::FLOAT32:
            writeAttribute(*attr.getData<float>(), attr.getDim());
            break;
        case DataType::FLOAT64:
            writeAttribute(*attr.getData<double>(), attr.getDim());
            break;
        }
        // 32b int to be changed
        if ((attr.getIndexRef() == -1) || (attr.getIndexCnt() > 0)) // we should not have index ref >= 0 and count >0 
        {
            const auto indicesPtr = attr.getIndices<int32_t>();
            if (indicesPtr)
                writeAttribute(*indicesPtr, (attr.getDomain() == eb::AttrDomain::PER_FACE) ? 1 : 3); // triangles only, or per face
        }
    }

    fout.close();
    return true;
}