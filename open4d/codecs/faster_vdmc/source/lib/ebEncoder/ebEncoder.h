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

#ifndef _EB_ENCODER_H_
#define _EB_ENCODER_H_

#include <fstream>
#include <stack>

// internal headers
#include "ebModel.h"
#include "ebCTMeshExt.h"
#include "ebConfig.h"
#include "ebBitstream.h"

namespace eb {

    class EBEncoder {
        
    public:

        EBConfig cfg;

        // parameters (shall be moved in EBConfig ?)
        int  qp = 12, qt = 11, qn = 16, qc = 8, qg = 11, qm = 8;
        bool yuv = false; // not yet implemented

     public:
        
        virtual ~EBEncoder() = default;

        virtual void encode(const Model& input_orig) = 0;
        virtual bool save(std::string fileName) = 0;
        virtual bool serialize(Bitstream& bitstream) = 0;

    protected:
        
        // The corner table representation of the mesh set
        CTMeshExt _ctMesh;  

        // bounding boxes generated and used by quantization moved to per attribute
        // will be encoded in bitstream in case quantization is enabled

        // quantizes the CTModel
        bool quantize(bool intAttr);

        uint32_t inline ConvertSignedIntToSymbol(int32_t val) {
            if (val >= 0) {// Early exit if val is positive... this maps 0 to 1... could be changed to keep 0 @ 0 ?
                return (val) << 1;
            }
            val = -(val + 1);  // Map -1 to 0, -2 to -1, etc..
            uint32_t ret = (val);
            ret <<= 1;
            ret |= 1;
            return ret;
        }
    };


};  // namespace eb

#endif
