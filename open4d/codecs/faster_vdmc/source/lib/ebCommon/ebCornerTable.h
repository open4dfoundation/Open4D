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

#ifndef _EB_CORNER_TABLE_H_
#define _EB_CORNER_TABLE_H_

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

    // Corner Table representation of Mesh
    // also named as Oposite Vertex table (OVTable)
   class CornerTable {
    public:

        std::vector<int> O;				  // Opposite vertex table
        std::vector<int> V;				  // Vertex indices table

    public:

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
    };
}

#endif