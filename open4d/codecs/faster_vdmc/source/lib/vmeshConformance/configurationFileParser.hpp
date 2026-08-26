/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ITU/ISO/IEC
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
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
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

#ifndef VMCConfigurationFleParser_h
#define VMCConfigurationFleParser_h

#include "vmc.hpp"

namespace vmesh {

const std::vector<std::string> bitStrMD5Keys = {"BITSTRMD5"};

const std::vector<std::string> hlsKeys = {"AtlasFrameIndex", 
										  "HLSMD5",   
										  "SEI11MD5", 
										  "SEI19MD5",
                                          "SEI64MD5",
                                          "SEI65MD5",
										  "SEI66MD5", 
										  "SEI67MD5",
                                          "SEI192-00MD5",
                                          "SEI192-01MD5",
                                          "SEI192-02MD5",
                                          "SEI192-03MD5",
                                          "SEI192-04MD5",
                                          "SEI192-05MD5" };

const std::vector<std::string> atlasKeys = {"AtlasFrameIndex",
                                            "AtlasFrameOrderCntVal",
                                            "AtlasID",
                                            "AtlasFrameWidth",
                                            "AtlasFrameHeight",
                                            "ASPSFrameSize",
                                            "VPSMapCount",
                                            "AttributeCount",
                                            "AttributeFrameWidthMax",
                                            "AttributeFrameHeightMax",
                                            "AttributeFrameSizeMax",
											"BasemeshAttributeCount",
                                            "NumGeometryTilesAtlasFrame",
											"NumAttributeTilesAtlasFrameMax",
											"NumSubmeshes",
                                            "AtlasTotalMeshpatches",
                                            "ATLASMD5"};

const std::vector<std::string> tileKeys = {"AtlasFrameIndex", 
										   "TileID",      
										   "AtlasFrameOrderCntVal",
                                           "TileType",        
										   "TileOffsetX", 
										   "TileOffsetY",
                                           "TileWidth",       
										   "TileHeight", 
										   "TileTotalMeshpatches",										   
										   "TILEMD5"};

const std::vector<std::string> mframeKeys = {"AtlasFrameIndex", 
											 "MeshFrameOrderCntVal", 
											 "NumSubmeshes", 
											 "NumVertices",
											 "MD5checksum"};

const std::vector<std::string> recMframeKeys = {"AtlasFrameIndex", 
												"MeshFrameOrderCntVal",
											    "NumVertices", 
												"MD5checksum"};

const std::vector<std::string> pictureKeys = {"Occupancy",
                                              "Geometry",
                                              "Attribute",
                                              "PackedVideo",
                                              "MapIdx",
                                              "AuxiliaryVideoFlag",
                                              "PicOrderCntVal",
                                              "IdxOutOrderCntVal",
                                              "AttrIdx",
                                              "AttrPartIdx",
                                              "AttrTypeID",
                                              "Width",
                                              "Height",
                                              "MD5checksumChan0",
                                              "MD5checksumChan1",
                                              "MD5checksumChan2"};

const std::vector<std::string> basemeshMD5Keys = {"IdxOutOrderCntVal",
												  "NumSubmeshes",
												  "SubmeshID",
												  "NumVertices",
												  "NumAttributes",
												  "MD5checkSumSubmesh"};

const std::vector<std::string> acDispMD5Keys = {"IdxOutOrderCntVal",
												"NumSubdisplacements",
												"SubdisplacementID",
												"NumDisplacements",
												"MD5checkSumSubdisplacement"};

const size_t AttributeAtlasLevelTable[3][1] = {  // Table A-7
    {2228224}, //1.0
    {8912896}, //2.0
    {35651584} //3.0
};

enum AttributeAtlasLimitType {  // Table A-7
  MaxAttributeAtlasSize = 0
};

const size_t GeometryAtlasLevelTable[4][5] = {  // Table A-8
  {1,16,1,16384}, //1.0
  {1,16,1,65536}, //2.0
  {2,32,2,524288}, //2.1
  {4,64,2,1048576} //2.2
};

enum GeometryAtlasLimitType {  // Table A-8
  MaxNumSubmeshes = 0,
  MaxCABSize,
  MaxNumTiles,
  MaxGeoAtlasSize
};

const size_t VDMCLevelTable[4][4] = {  // Table A-9
    {16,	240,	30,	491520}, //1.0
    {16,	240,	30,	1966080}, //2.0
    {32,	480,	60,	15728640}, //2.1
    {64,	960,	120, 31457280} //2.2
};

enum VDMCLimitType {  // Table A-9
  MaxAtlasBR = 0,
  MaxMeshpatchesPerSec,
  MaxSubmeshesPerSec,
  MaxAggregateDisplSr
};

const size_t BasemeshLevelTable[4][2] = {  // Table A-10
    {4000, 120000}, //1.0
    {8000, 240000}, //2.0
    {12000, 360000}, //2.1
    {24000, 720000} //2.2
};

enum BasemeshLimitType {  // Table A-10
  MaxVerticesPerFrame = 0,
  MaxAggregateVertexRate
};


struct VMCErrorMessage {
  VMCErrorMessage( bool flg = false ) : is_errored_( flg ){};
  VMCErrorMessage( const VMCErrorMessage& rep ) : is_errored_( rep.is_errored_ ){};
  virtual ~VMCErrorMessage() {}
  virtual std::ostream& error( const std::string& errMessage, const std::string& where = "" );
  virtual std::ostream& warn( const std::string& errMessage, const std::string& where = "" );
  bool                  is_errored_;
};

struct VMCDynamicData {
  VMCDynamicData() { data_[0] = data_[1] = data_[2] = data_[3] = 0; }
  VMCDynamicData( size_t meshpatches, size_t submeshes, size_t bmvertices, size_t displacements) {
    data_[0] = meshpatches, data_[1] = submeshes;
    data_[2] = bmvertices; data_[3] = displacements;
  }

  VMCDynamicData& operator+=( const VMCDynamicData& rhs ) {
    data_[0] += rhs.data_[0];
    data_[1] += rhs.data_[1];
    data_[2] += rhs.data_[2];
    data_[3] += rhs.data_[3];
    return *this;
  }

  VMCDynamicData& operator-=( const VMCDynamicData& rhs ) {
    data_[0] -= rhs.data_[0];
    data_[1] -= rhs.data_[1];
    data_[2] -= rhs.data_[2];
    data_[3] -= rhs.data_[3];
    return *this;
  }
  size_t data_[4];  // data[0] -> mehspatches, data[1] -> submeshes, data[2] -> basemes vertices, data[3] -> AC displacements ;
};

typedef std::vector<std::map<std::string, std::string>> KeyValMaps;
typedef std::map<std::string, std::string>              StringStringMap;

class VMCConfigurationFileParser : VMCErrorMessage {
 public:
  VMCConfigurationFileParser( const std::vector<std::string>& keys ) :
      VMCErrorMessage(),
      name_( "" ),
      linenum_( 0 ),
      keyList_( keys ){};

  const std::string where() {
    std::ostringstream os;
    os << "File Name: " << name_;
    if ( linenum_ >= 0 ) os << "\nLine Number: " << linenum_;
    return os.str();
  }

  bool parseFile( std::string& fileName, KeyValMaps& key_val_maps );
  void scanLine( std::string& line, KeyValMaps& key_val_maps );
  void scanStream( std::istream& in, KeyValMaps& key_Val_maps );
  bool validKey( std::string& key );

 private:
  std::string name_;
  int         linenum_;

  const std::vector<std::string>& keyList_;
};

}  // namespace VMC

#endif  //~VMCConfigurationFleParser_h
