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

#ifndef _EB_CTMESH_H_
#define _EB_CTMESH_H_

//
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
// mathematics
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

namespace eb {

    // Corner Table (Oposite Vertex table - OVTable) representation of Mesh
    class CTMesh {
    public:

        std::vector<glm::vec3>       positions;    // vertex positions (x,y,z)
        std::vector<glm::vec2>       uvcoords;     // vertex uv coordinates (u,v)
        std::vector<glm::vec3>       normals;      // vertex normals (x,y,z)
        std::vector<glm::vec3>       colors;       // vertex colors (r,g,b)
        std::vector<int>             ids;          // per triangle face IDs

        std::vector<int> O;				  // Opposite table
        std::vector<int> V;				  // Vertex indices table
        std::vector<int> OTC;		      // Opposite table for TexCoord attribs when separate index used
        std::vector<int> TC;			  // TexCoord indices table

        // information for deduplication when convertModelToCTMesh called with keepTrackOfVertexDeduplications
        std::map<int, int> duplicatesMap; // associate groups of duplicated points to a unique index from 0 to numSplitVertices minus 1
        std::map<int, int> duplicatesUVMap;  //associate groups of duplicated uv points to a unique index from 0 to numSplitUVVertices minus 1 when UV uses auxiliary index table

    public:

        inline int getPositionCount() const { return (int)positions.size(); }
        inline int getUVCount() const { return (int)uvcoords.size(); }
        inline int getTriangleCount() const { return (int)ntri(); }

        inline int ntri() const {
            return (int)V.size() / 3;
        }

        // c.v
        inline int v(const int c) const {
            return (c < 0 || c >= (signed)V.size()) ? -1 : V[c];
        }

        // c.o
        inline int o(const int c) const {
            return (c < 0 || c >= (signed)O.size()) ? -1 : O[c];
        }

        // c.n = 3 * c.t + (c + 1) mod 3
        inline int n(int c) const
        {
            return (c < 0) ? -1 : (++c % 3 ? c : c - 3); // faster. Note that <0 test only for swing in model converter when in reverse
            //return (c < 0) ? -1 : (3 * (c / 3) + (c + 1) % 3); // previous implementation
        }

        // c.p = 3 * c.t + (c + 2) mod 3
        inline int p(int c) const
        {
            return (c < 0) ? -1 : (c % 3 ? c - 1 : c + 2); // faster. Note that <0 test only for swing in model converter when in reverse
            //return (c < 0) ? -1 : (3 * (c / 3) + (c + 2) % 3); // previous implementation
        }

        // c.r = c.n.o
        inline int r(int c) const
        {
            return (c < 0) ? -1 : O[n(c)];
        }

        // c.l = c.p.o
        inline int l(int c) const
        {

            return (c < 0) ? -1 : O[p(c)];
        }

        //c.r = c.n.o 
        inline int r(int c, int* _O) const
        {

            return (c < 0) ? -1 : _O[n(c)];
        }

        // c.l = c.p.o
        inline int l(int c, int* _O) const
        {

            return (c < 0) ? -1 : _O[p(c)];
        }

        // c.t
        inline int t(int c) const
        {
            return (c < 0) ? -1 : (c / 3);
        }

        inline void setV(int c, int v = -1) {
            if (c > -1 && c < (signed)V.size()) V[c] = v;
        }
        inline void setO(int c, int o = -1) {
            if (c > -1 && c < (signed)O.size()) O[c] = o;
        }

        // Check if a vertex is on a boundary
        // return the boundary id or 0
        // supposes that boundaries are marked using negative opposites
        int isOnBoundary(const int c) const
        {
            const int _c = n(c);
            int b = _c;
            do { // turn right until we find a negative value
                b = r(b);
                if (b < 0) return b;
            } while (b != _c);

            return 0;
        }

        // Raw ASCII dump of the table
        // first OV table
        // then positions
        bool dumpAscii(std::string fileName) {
            std::ofstream out(fileName);
            if (!out)
                return false;

            for (auto i = 0; i < V.size() / 3; ++i) {
                out << V[i * 3 + 0] << " " << V[i * 3 + 1] << " " << V[i * 3 + 2] << " "
                    << O[i * 3 + 0] << " " << O[i * 3 + 1] << " " << O[i * 3 + 2] << " " << std::endl;
            }
            out << std::endl;
            for (auto i = 0; i < positions.size(); ++i) {
                out << positions[i].x << " " << positions[i].y << " " << positions[i].z << " " << std::endl;
            }
            out.close();
            return true;
        }

        // a.k.a fill all holes with triangle fans and center mean point
        // after the process there will be no more negative opposits 
        // return the number of boundaries that were closed
        int closeBoundaries(std::vector<int>& vertexIndices) {

            const auto nbCorners = V.size();

            for (auto c = 0; c < nbCorners; ++c) {
                // reset boundaries
                if (O[c] < 0)
                    O[c] = -2;
            }

            int nBounds = 0;
            std::vector<int> boundary;
            for (auto c = 0; c < nbCorners; ++c) {
                // means we are on a boundary corner
                if (O[c] == -2) {
                    vertexIndices.push_back(closeBoundary(n(c), boundary, nBounds));
                    // free mem for next boundary if any
                    boundary.clear();
                }
            }

            return nBounds;
        }

        // Goes through the boundary of b (O(b) must be < 0)
        // closes the surface using a new point and a triangle fan
        // return the index of the new vertex
        int closeBoundary(int _b, std::vector<int>& boundary, int& nBounds)
        {
            const bool hasUv = uvcoords.size();
            const bool hasCol = colors.size();
            const bool hasNrm = normals.size();

            // this processing should be replicate to other attribs having separate index tables
            const bool hasSeparateUvIndex = OTC.size();

            nBounds = nBounds + 1;

            auto b = n(_b);              // goes the next triangle, 
            while (r(b) >= 0) {          // goes to right towards the boundary
                b = r(b);
            }

            do {                         // starts the boundary encoding 

                b = n(b);                // starts from the next triangle
                O[b] = -(nBounds + 2);
                if (hasSeparateUvIndex)
                    OTC[b] = -(nBounds + 4); // and if uv index separate!! -5 on 1st bnd -2/-3/-4 used
                while (r(b) >= 0) {      // skip triangle fan
                    b = r(b);            // goes to the rightmost triangle
                }
                boundary.push_back(b);

            } while (r(b) != -(nBounds + 2)); // stops when all the boundary is marked 

            // reserve storage for the new triangles
            const auto offset = V.size();
            V.resize(offset + 3 * boundary.size(), -1);
            TC.resize(offset + 3 * boundary.size(), -1); // -1 index as a marker of external boundary
            O.resize(offset + 3 * boundary.size(), -2);
            if (hasSeparateUvIndex)
                OTC.resize(offset + 3 * boundary.size(), -(nBounds + 4));

            // reserve storage for the new vertex
            glm::dvec3 newPos(0.0, 0.0, 0.0);
            glm::dvec2 newUv(0.0, 0.0);
            glm::dvec3 newCol(0.0, 0.0, 0.0);
            glm::dvec3 newNrm(0.0, 0.0, 0.0);
            positions.push_back(glm::vec3(newPos));
            int newV = (int)(positions.size() - 1);
            if (hasUv)   uvcoords.push_back(glm::vec2(newUv));
            if (hasCol) colors.push_back(glm::vec3(newCol));
            if (hasNrm)  normals.push_back(glm::vec3(newNrm));

            //int nbVerts = 0;

            for (auto i = 0; i < boundary.size(); ++i) {

                const auto c1 = boundary[i];
                const auto c2 = boundary[(i + 1) % boundary.size()];

                // newPos += positions[V[c1]];  // add the vertex for final mean
                //nbVerts++;
                // not valid when separte index for tex coords
                //if (hasUv)  newUv  += uvcoords[V[c1]]; // no need to assign dummy values
                if (hasCol) newCol += colors[V[c1]];
                if (hasNrm) newNrm += normals[V[c1]];

                V[offset + i * 3 + 0] = V[c1]; // 
                V[offset + i * 3 + 1] = newV;
                V[offset + i * 3 + 2] = V[c2];

                // opposit of C1
                const auto nextC2 = offset + ((i + 1) % boundary.size() * 3 + 2);
                O[offset + i * 3 + 0] = nextC2;
                O[nextC2] = offset + i * 3 + 0;

                // opposit of new vert
                O[offset + i * 3 + 1] = n(c2);
                O[n(c2)] = offset + i * 3 + 1;

                // opposit of C2
                const auto prevC1 = offset + ((i - 1) % boundary.size() * 3);
                O[offset + i * 3 + 2] = prevC1;
                O[prevC1] = offset + i * 3 + 2;

            }

            //positions[newV] = glm::vec3(newPos / glm::dvec3(nbVerts));

            // beware with separate index there
            //if (hasUv)  uvcoords[newV] = glm::vec2(newUv  / glm::dvec2(nbVerts)); // no need to assign dummy values
            //if (hasCol) colors[newV] = glm::vec3(newCol / glm::dvec3(nbVerts));
            //if (hasNrm) normals[newV] = glm::vec3(newNrm / glm::dvec3(nbVerts));

            return newV;
        }
    };
}

#endif