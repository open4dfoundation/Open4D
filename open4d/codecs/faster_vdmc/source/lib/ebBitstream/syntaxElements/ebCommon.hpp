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
#pragma once

namespace eb {

enum class MeshCodecType {
	CODEC_TYPE_REVERSE = 0,  // 0
    CODEC_TYPE_RESERVED_1,   // 1
	CODEC_TYPE_RESERVED_2,   // 2
	CODEC_TYPE_RESERVED_3    // 3
};

enum class MeshVertexTraversalMethod {
		MESH_EB_TRAVERSAL = 0,      // 0
		MESH_DEGREE_TRAVERSAL = 1,  // 1
		MESH_TRAVERSAL_RESERVED_2,  // 2
	    MESH_TRAVERSAL_RESERVED_3,  // 3
};

enum class MeshPositionPredictionMethod {
	MESH_POSITION_MPARA = 0,  // 0
	// MESH_POSITION_RESERVED >0
};

enum class MeshDeduplicateMethod {
	MESH_DEDUP_NONE = 0,  // 0
	MESH_DEDUP_DEFAULT   // 1
	// MESH_DEDUP_RESERVED > 1
};

enum class MeshAttributeType {
	MESH_ATTR_TEXCOORD = 0,  // 0
	MESH_ATTR_NORMAL,        // 1
	MESH_ATTR_COLOR,         // 2
	MESH_ATTR_MATERIAL_ID,   // 3
	MESH_ATTR_GENERIC,       // 4
	MESH_ATTR_RESERVED_5,    // 5
	MESH_ATTR_RESERVED_6,    // 6
	MASH_ATTR_UNSPECIFIED    // 7
};

enum class MeshAttributePredictionMethod_TEXCOORD {
	MESH_TEXCOORD_STRETCH = 0  // 0
	// MESH_TEXCOORD_RESERVED >0
};

enum class MeshAttributePredictionMethod_NORMAL {
	MESH_NORMAL_DELTA = 0,  // 0
    MESH_NORMAL_MPARA = 1,
    MESH_NORMAL_CROSS = 2,
	// MESH_NORMAL_RESERVED >2
};

enum class MeshAttributePredictionMethod_COLOR {
	MESH_COLOR_DEFAULT = 0  // 0
	// MESH_COLOR_RESERVED >0
};

enum class MeshAttributePredictionMethod_MATERIALID {
	MESH_MATERIALID_DEFAULT = 0  // 0
	// MESH_MATERIALID_RESERVED >0
};

enum class MeshAttributePredictionMethod_GENERIC {
	MESH_GENERIC_DELTA = 0,  // 0
	MESH_GENERIC_MPARA = 1,
	// MESH_GENERIC_RESERVED >1
};


// no !! cleanup
template <MeshAttributeType a> class MeshAttributePrediction {
};

template<> class MeshAttributePrediction<MeshAttributeType::MESH_ATTR_TEXCOORD> {
	typedef enum MeshAttributePredictionMethod_TEXCOORD Method;
};
template<> class MeshAttributePrediction<MeshAttributeType::MESH_ATTR_NORMAL> {
	typedef enum MeshAttributePredictionMethod_NORMAL Method;
};
template<> class MeshAttributePrediction<MeshAttributeType::MESH_ATTR_COLOR> {
	typedef enum MeshAttributePredictionMethod_COLOR Method;
};
template<> class MeshAttributePrediction<MeshAttributeType::MESH_ATTR_MATERIAL_ID> {
	typedef enum MeshAttributePredictionMethod_MATERIALID Method;
};
template<> class MeshAttributePrediction<MeshAttributeType::MESH_ATTR_GENERIC> {
	typedef enum MeshAttributePredictionMethod_GENERIC Method;
};

};  // namespace eb
