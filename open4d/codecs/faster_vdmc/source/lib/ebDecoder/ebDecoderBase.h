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

#ifndef _EB_DECODER_BASE_H_
#define _EB_DECODER_BASE_H_

#include <fstream>
#include <stack>

// internal headers
#include "ebModel.h"
#include "ebCTMeshExt.h"
#include "ebConfig.h"

#include "ebBitstream.h"
#include "ebReader.hpp"
#include "syntaxElements/meshCoding.hpp"

namespace eb {

    class EBDecoderBase {

    public: // methods
        
        EBConfig cfg;

        EBDecoderBase() {};

        virtual ~EBDecoderBase() {};

        // stream will be close by caller
        virtual bool load(std::ifstream& in) {
            std::cout << "Error: decoder load method is not implemented" << std::endl;
            return false;
        }

        virtual bool unserialize(MeshCoding& meshCoding) {
            std::cout << "Error: decoder unserialize method is not implemented" << std::endl;
            return false;
        }

        virtual bool decode(Model& output)=0;

    protected:

        // The corner table representation of the mesh set
        CTMeshExt _ctMesh;

        // Converts a single unsigned integer symbol back to a signed value.
        int32_t inline ConvertSymbolToSignedInt(uint32_t val) {
            const bool is_positive = !static_cast<bool>(val & 1);
            val >>= 1;
            if (is_positive) {
                return (val);
            }
            int32_t ret = (val);
            ret = -ret - 1;
            return ret;
        }
    };
};  // namespace mm

#endif
