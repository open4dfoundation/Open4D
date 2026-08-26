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

#ifndef _MM_EB_CONFIG_H_
#define _MM_EB_CONFIG_H_

#define ENABLE_GENERIC_FXP 1

namespace eb {

    class EBConfig  {

    public:

        // name of the traversal method
        enum class Traversal {
            EB = 0,     // use vertex traversal from edge breaker topology traversal
            DEGREE = 1, // use prediction degree vertex traversal
        };

        // name of the reindex method
        enum class Reindex {
            EB = 0,     // force use vertex traversal from edge breaker topology traversal
            DEGREE = 1, // force use prediction degree vertex traversal
            DEFAULT = 2,// use method from bitstream
        };

        // name of the prediction mode for positions
        enum class PosPred {
            MPARA = 0,  // use multi- parallelogram rule
        };

        // name of the prediction mode for positions
        enum class UvPred {
            STRETCH = 0 // uv stretch
        };

        enum class NormPred {
          DELTA     = 0,  // use delta
          MPARA     = 1,     // use multi-parallelogram.
          CROSS   = 2,   // use cross product during edgebreaker.
        };

        enum class GenPred {
            DELTA = 0,  // use delta
            MPARA = 1,     // use multi-parallelogram.
        };

    public:
        // log switch
        bool verbose=true;
        // traversal mode
        Traversal traversal = Traversal::DEGREE;
        // outpurt reindex mode
        Reindex reindexOutput = Reindex::DEFAULT; // stands to value from the bitstream
        // position prediction mode
        PosPred posPred = PosPred::MPARA;
        // uv coords prediction mode
        UvPred uvPred = UvPred::STRETCH;
        // Consider input attributes (pos and uv) as integers stored on the number of bits resp. defined by --qp and --qt, --qm
        bool intAttr = false;
        // Deduplicate vertices positions added to handle non - manifold meshes on decode(adds related information in the bitstream)
        bool deduplicate = false;
        // Unify vertices positions and or attributes in a pre-processing step
        bool unify = false;
        // Unify vertices positions only - for the very specific requirement of reverse unification
        bool unifyPositionsOnly = false;
        // Recover duplicate vertices positions
        bool reverseUnification = false;
        // flag options
        uint32_t optionFlags = 0;
        // Use main index table if aux index equals main 
        bool useMainIndexIfEqual = true;
        
        // Normals
        bool hasNormals = false;            // whether input mesh has normals or not
        NormPred normPred = NormPred::CROSS;
        int  qpOcta = 16;
        // To use Octahedral 2D representation or not
        bool useOctahedral = true;
        bool normalEncodeSecondResidual = true;
        bool useEntropyPacket = false;
        
        /* Wrap Around
        This employs the min and max limits of the original normal to wrap the 
        residual values around zero. Original (O), Residual (R), Prediction (P),
        N = MAX - MIN. The residual, R = O - P, can be stored as:
               R + N,   if R < -N / 2
               R - N,   if R > N / 2
               R        otherwise
        To unwrap, the decoder checks whether the final reconstructed value 
        F = P + R is out of the bounds of the input values.
        All out of bounds values are unwrapped using
               F + N,   if F < MIN
               F - N,   if F > MAX
        This wrapping can reduce the number of unique values, which translates to a
        better entropy of the stored values and better compression rates. */
        bool wrapAround = true;
        
        // Generic
        GenPred genPred = GenPred::DELTA;
    };

};  // namespace mm

#endif
