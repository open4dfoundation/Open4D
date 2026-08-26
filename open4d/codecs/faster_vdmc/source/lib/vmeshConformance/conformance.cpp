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
#include "conformance.hpp"
#include "conformanceParameters.hpp"
#include "configurationFileParser.hpp"

using namespace std;
using namespace vmesh;

VMCConformance::VMCConformance() :
    levelLimitsCount_( 0 ),
    levelLimitsExceedCount_( 0 ),
    logFilesCount_( 0 ),
    logFilesMatchCount_( 0 ),
    logFileTestsMatch_( true ),
    levelLimitTestsMatch_( true ) {}
VMCConformance::~VMCConformance() {}

void VMCConformance::check( const VMCConformanceParameters& params , int gofIndex) {
  VMCErrorMessage errMsg;
  double          aR = 1. / (double)params.fps_;
  KeyValMaps      key_val_decA, key_val_decB;
  logFilesCount_      = 0;
  logFilesMatchCount_ = 0;
  levelLimitsCount_   = 0;

  std::string fileDecA = params.path_ + "_conformance_enc_bitstream_md5.txt";
  std::string fileDecB = params.path_ + "_conformance_dec_bitstream_md5.txt";
  cout << "\n ^^^^^^Checking Bitstream MD5^^^^^^ \n";
  logFileTestsMatch_ = true;
  if ( !compareLogFiles( fileDecA, fileDecB, bitStrMD5Keys, key_val_decA, key_val_decB ) ) {
    std::string tmp = fileDecA + " with " + fileDecB;
    errMsg.warn( "\n ******* Please Check ******* \n", tmp );
  }
  cout << "^^^^^^ BitStream MD5 : " << ( logFileTestsMatch_ ? "MATCH" : "DIFF" ) << std::endl;

  fileDecA = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_hls_md5.txt";
  fileDecB = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_hls_md5.txt";
  key_val_decA.clear();
  key_val_decB.clear();

  cout << "\n ^^^^^^Checking High Level Syntax MD5^^^^^^ \n";
  logFileTestsMatch_ = true;
  if ( !compareLogFiles( fileDecA, fileDecB, hlsKeys, key_val_decA, key_val_decB ) ) {
    std::string tmp = fileDecA + " with " + fileDecB;
    errMsg.warn( "\n ******* Please Check ******* \n", tmp );
  }
  cout << "^^^^^^ High Level Syntax MD5 : " << ( logFileTestsMatch_ ? "MATCH" : "DIFF" ) << std::endl;

  fileDecA = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_atlas_log.txt";
  fileDecB = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_atlas_log.txt";
  key_val_decA.clear();
  key_val_decB.clear();
  cout << "\n ^^^^^^ Atlas Log Files Check ^^^^^^ \n";
  logFileTestsMatch_ = true;
  if ( !compareLogFiles( fileDecA, fileDecB, atlasKeys, key_val_decA, key_val_decB ) ) {
    std::string tmp = fileDecA + " with " + fileDecB;
    errMsg.warn( "\n ******* Please Check ******* \n", tmp );
  }
  cout << "^^^^^^ Atlas Log Files " << ( logFileTestsMatch_ ? "MATCH" : "DIFF" ) << std::endl;

  cout << "\n ^^^^^^ Atlas Level Limits Test  ^^^^^^ \n";
  levelLimitTestsMatch_ = true;
  checkAtlasLevelLimits( params.levelIdc_, params.levelAttrIdc_, aR, key_val_decB );
  cout << "^^^^^^ Atlas Level Limits: " << ( levelLimitTestsMatch_ ? "MATCH" : "DIFF" ) << std::endl;

  key_val_decA.clear();
  key_val_decB.clear();
  cout << "\n ^^^^^^ Tile Log Files Check ^^^^^^\n";
  logFileTestsMatch_ = true;
  fileDecA           = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_tile_log.txt";
  fileDecB           = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_tile_log.txt";
  if ( !compareLogFiles( fileDecA, fileDecB, tileKeys, key_val_decA, key_val_decB ) ) {
    std::string tmp = fileDecA + " with " + fileDecB;
    errMsg.warn( "\n ******* Please Check ******* \n", tmp );
  }
  cout << "^^^^^^ Tile Logs: " << ( logFileTestsMatch_ ? "MATCH" : "DIFF" ) << std::endl;

  key_val_decA.clear();
  key_val_decB.clear();
  cout << "\n ^^^^^^ Mesh Frame Log Files Check ^^^^^^ \n";
  logFileTestsMatch_ = true;
  fileDecA           = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_mframe_log.txt";
  fileDecB           = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_mframe_log.txt";
  logFileTestsMatch_ = true;
  if ( !compareLogFiles( fileDecA, fileDecB, mframeKeys, key_val_decA, key_val_decB ) ) {
    std::string tmp = fileDecA + " with " + fileDecB;
    errMsg.warn( "\n ******* Please Check  ******* \n", tmp );
  }
  cout << "^^^^^^ Mesh Frame Log Files : " << ( logFileTestsMatch_ ? "MATCH" : "DIFF" ) << std::endl;

  key_val_decA.clear();
  key_val_decB.clear();
  logFileTestsMatch_ = true;
  cout << "\n ^^^^^^ Post Reconstruction Mesh Frame Log Files Check ^^^^^^ \n";
  fileDecA = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_rec_mframe_log.txt";
  fileDecB = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_rec_mframe_log.txt";
  if ( !compareLogFiles( fileDecA, fileDecB, recMframeKeys, key_val_decA, key_val_decB ) ) {
    std::string tmp = fileDecA + " with " + fileDecB;
    errMsg.warn( "\n ******* Please Check ******* \n", tmp );
  }
  cout << "^^^^^^ Post Reconstruction Mesh Frame Log Files: " << ( logFileTestsMatch_ ? "MATCH" : "DIFF" )
       << std::endl;

  key_val_decA.clear();
  key_val_decB.clear();
  logFileTestsMatch_ = true;
  cout << "\n ^^^^^^ Picture Log Files Check ^^^^^^";
  fileDecA = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_picture_log.txt";
  fileDecB = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_picture_log.txt";
  if ( !compareLogFiles( fileDecA, fileDecB, pictureKeys, key_val_decA, key_val_decB ) ) {
    std::string tmp = fileDecA + " with " + fileDecB;
    errMsg.warn( "\n ******* Please Check ******* \n", tmp );
  }
  cout << "^^^^^^  Picture Log Files: " << ( logFileTestsMatch_ ? "MATCH" : "DIFF" ) << std::endl;

  fileDecA = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_basemesh_log.txt";
  fileDecB = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_basemesh_log.txt";
  key_val_decA.clear();
  key_val_decB.clear();

  cout << "\n ^^^^^^Checking Basemesh MD5^^^^^^ \n";
  logFileTestsMatch_ = true;
  if (!compareLogFiles(fileDecA, fileDecB, basemeshMD5Keys, key_val_decA, key_val_decB)) {
      std::string tmp = fileDecA + " with " + fileDecB;
      errMsg.warn("\n ******* Please Check ******* \n", tmp);
  }
  cout << "^^^^^^ Basemesh MD5 : " << (logFileTestsMatch_ ? "MATCH" : "DIFF") << std::endl;

  cout << "\n ^^^^^^ Basemesh Frame Level Limits Test  ^^^^^^\n";
  levelLimitTestsMatch_ = true;
  checkBasemeshLevelLimits(params.levelIdc_, aR, key_val_decB);
  cout << "^^^^^^ Basemesh Frame Level Limits: " << (levelLimitTestsMatch_ ? "MATCH" : "DIFF") << std::endl;

  fileDecA = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_enc_ac_displacement_log.txt";
  fileDecB = params.path_ + "_GOF_" + std::to_string(gofIndex) + "_conformance_dec_ac_displacement_log.txt";
  key_val_decA.clear();
  key_val_decB.clear();

  cout << "\n ^^^^^^Checking AC Displacement MD5^^^^^^ \n";
  logFileTestsMatch_ = true;
  if (!compareLogFiles(fileDecA, fileDecB, acDispMD5Keys, key_val_decA, key_val_decB)) {
      std::string tmp = fileDecA + " with " + fileDecB;
      errMsg.warn("\n ******* Please Check ******* \n", tmp);
  }
  cout << "^^^^^^ AC Displacement MD5 : " << (logFileTestsMatch_ ? "MATCH" : "DIFF") << std::endl;
  cout << "\n ^^^^^^ AC Displacement Frame Level Limits Test  ^^^^^^\n";
  levelLimitTestsMatch_ = true;
  checkACDispLevelLimits(params.levelIdc_, aR, key_val_decB);
  cout << "^^^^^^ AC Displacement Frame Level Limits: " << (levelLimitTestsMatch_ ? "MATCH" : "DIFF") << std::endl;

  cout << "\n File Check Tests (matched / total): " << logFilesMatchCount_ << " / " << logFilesCount_ << std::endl;
  cout << "\n Level Limits Tests ( passed / total): " << ( levelLimitsCount_ - levelLimitsExceedCount_ ) << " / "
       << levelLimitsCount_ << std::endl;
}

bool VMCConformance::compareLogFiles( std::string&                    fNameEnc,
                                      std::string&                    fNameDec,
                                      const std::vector<std::string>& keyList,
                                      KeyValMaps&                     key_val_enc,
                                      KeyValMaps&                     key_val_dec ) {
  VMCConfigurationFileParser cfr( keyList );
  if ( !cfr.parseFile( fNameEnc, key_val_enc ) ) {
    cout << " Encoder File " << fNameEnc << " not exist \n";
    logFileTestsMatch_ = false;
    logFilesCount_++;
    return false;
  }
  if ( !cfr.parseFile( fNameDec, key_val_dec ) ) {
    cout << " Decoder File " << fNameDec << " not exist \n";
    logFileTestsMatch_ = false;
    logFilesCount_++;
    return false;
  }
  if ( key_val_enc.size() != key_val_dec.size() ) {
    cout << " Encoder File Has " << key_val_enc.size() << " Lines \n";
    cout << " Decoder File Has " << key_val_dec.size() << " Lines \n";
    logFileTestsMatch_ = false;
    logFilesCount_++;
    return false;
  }
  size_t index = 0;
  for ( auto& key_val : key_val_enc ) {
    StringStringMap&          dec = key_val_dec[index++];
    StringStringMap::iterator it;
    for ( it = key_val.begin(); it != key_val.end(); ++it ) {
      if ( !dec[it->first].compare( it->second ) ) {
        if ( it->first == "Occupancy" || it->first == "Geometry" || it->first == "Attribute" || it->first == "PackedVideo") {
          cout << "\n-------" << it->first << "-------\n" << endl;
          continue;
        }
        if ( it->first == "AtlasFrameIndex" ) cout << "\n";
        cout << "(Enc: " << left << setw( 30 ) << it->first << ", " << setw( 32 ) << it->second << " )"
             << " **MATCH** ";
        cout << "(Dec: " << left << setw( 30 ) << it->first << ", " << setw( 32 ) << dec[it->first] << " )" << endl;
        logFilesMatchCount_++;
      } else {
        cout << "(Enc: " << left << setw( 30 ) << it->first << ", " << setw( 32 ) << it->second << " )"
             << " **DIFF**  ";
        cout << "(Dec: " << left << setw( 30 ) << it->first << ", " << setw( 32 ) << dec[it->first] << " )" << endl;
        logFileTestsMatch_ = false;
      }
      logFilesCount_++;
    }
  }
  return true;
}

void VMCConformance::checkAtlasLevelLimits( uint8_t levelIdc, uint8_t levelAttrIdc, double aR, KeyValMaps& key_val_map) {
  std::map<std::string, size_t> maxLevelLimit;
  std::map<std::string, size_t> maxLevelLimitPerSecond;
  std::deque<VMCDynamicData>    dataWindow;
  std::vector<std::string>      agrData;
  VMCErrorMessage               error_rep;
  agrData.resize( 2 );

  int64_t clockTick     = -1;
  int64_t frmPerSecMin1 = ( int64_t )( 1 / aR ) - 1;
  uint8_t levelIdx = (uint8_t)((levelIdc / 30.0 - 1) + ((levelIdc % 30) / 3.0));
  uint8_t levelAttrIdx = (uint8_t)((levelAttrIdc / 30.0 - 1));
  if ( levelIdx >= 4 ) {
    error_rep.error(
        " Dynamic Conformance Check Cannot Be Done: level indicator should be in multiples of 30 i.e. 30 - 66 for "
        "levels 1 to 2.2 \n" );
    levelLimitTestsMatch_ = false;
    levelLimitsCount_++;
    return;
  }
  if (levelAttrIdx >= 3) {
    error_rep.error(
      " Dynamic Conformance Check Cannot Be Done: attribute level indicator should be in multiples of 30 i.e. 30 - 90 for "
      "levels 1 to 3.0 \n");
    levelLimitTestsMatch_ = false;
    levelLimitsCount_++;
    return;
  }
  maxLevelLimit.emplace( "AttributeFrameSizeMax", AttributeAtlasLevelTable[levelAttrIdx][MaxAttributeAtlasSize] );
  maxLevelLimit.emplace("NumSubmeshes", GeometryAtlasLevelTable[levelIdx][MaxNumSubmeshes]);
  maxLevelLimit.emplace( "NumGeometryTilesAtlasFrame", GeometryAtlasLevelTable[levelIdx][MaxNumTiles] );
  maxLevelLimit.emplace("ASPSFrameSize", GeometryAtlasLevelTable[levelIdx][MaxGeoAtlasSize]);
  maxLevelLimit.emplace("AtlasTotalMeshpatches", 8*GeometryAtlasLevelTable[levelIdx][MaxNumSubmeshes]);
  maxLevelLimitPerSecond.emplace("NumSubmeshes", VDMCLevelTable[levelIdx][MaxSubmeshesPerSec]);
  maxLevelLimitPerSecond.emplace("AtlasTotalMeshpatches", VDMCLevelTable[levelIdx][MaxMeshpatchesPerSec]);
  agrData[0] = "AtlasTotalMeshpatches";
  agrData[1] = "NumSubmeshes";


  // check general tier level limits for Tables A.7 & A.8
  size_t         frmIdx, value, maxValue;
  VMCDynamicData totalPerSec{};
  for ( auto& key_val_pair : key_val_map ) {
    VMCDynamicData tmp;
    bool           skipLineFlag = false;
    for ( auto& kvp : key_val_pair ) {
      if ( kvp.first == "AtlasFrameIndex" ) {
        convertString( kvp.second, frmIdx );
        //skipLineFlag = true;
        clockTick++;
      }
      if ( !maxLevelLimit.count( kvp.first ) ) continue;
      if ( !checkLimit( kvp.second, maxLevelLimit[kvp.first], value ) ) {
        cout << "\n" << kvp.first << " Value : " << value << " Exceeds Max. Limit " << maxLevelLimit[kvp.first] << endl;
        levelLimitsExceedCount_++;
        levelLimitsCount_++;
        levelLimitTestsMatch_ = false;
      }
      if ( kvp.first == agrData[0] || kvp.first == agrData[1] ) {
        for ( int n = 0; n < 2; n++ ) {
          if ( kvp.first == agrData[n] ) {
            convertString( kvp.second, tmp.data_[n] );
            break;
          }
        }
      }
    }
    if ( !skipLineFlag ) {
      totalPerSec += tmp;
      dataWindow.push_back( tmp );
      if ( clockTick >= frmPerSecMin1 ) {
        for ( int n = 0; n < 2; n++ ) {
          maxValue = maxLevelLimitPerSecond[agrData[n]];
          if ( totalPerSec.data_[n] > maxValue ) {
            printf( " %s MaxPerSec %zu Exceeds Table A-9 Specified Limit %zu \n", agrData[n].c_str(),
                    totalPerSec.data_[n], maxValue );
            levelLimitTestsMatch_ = false;
          }
        }
        totalPerSec -= dataWindow.front();
        dataWindow.pop_front();
      }
    }
  }
  levelLimitsCount_++;
}

void VMCConformance::checkBasemeshLevelLimits(uint8_t levelIdc, double aR, KeyValMaps& key_val_map) {
  std::map<std::string, size_t> maxLevelLimit;
  std::map<std::string, size_t> maxLevelLimitPerSecond;
  std::deque<VMCDynamicData>    dataWindow;
  std::string                   agrData;
  VMCErrorMessage               error_rep;

  int64_t clockTick = -1;
  int64_t frmPerSecMin1 = (int64_t)(1 / aR) - 1;
  uint8_t levelIdx = (uint8_t)((levelIdc / 30.0 - 1) + ((levelIdc % 30) / 3.0));
  if (levelIdx >= 4) {
    error_rep.error(
      " Dynamic Conformance Check Cannot Be Done: level indicator should be in multiples of 30 i.e. 30 - 66 for "
      "levels 1 to 2.2 \n");
    levelLimitTestsMatch_ = false;
    levelLimitsCount_++;
    return;
  }
  maxLevelLimit.emplace("NumVertices", BasemeshLevelTable[levelIdx][MaxVerticesPerFrame]);
  maxLevelLimitPerSecond.emplace("NumVertices", BasemeshLevelTable[levelIdx][MaxAggregateVertexRate]);
  agrData = "NumVertices";

  // check general tier level limits A.6.1 & A.6.2
  size_t         frmIdx, value, maxValue;
  VMCDynamicData totalPerSec{};
  for (auto& key_val_pair : key_val_map) {
    VMCDynamicData tmp;
    bool           skipLineFlag = false;
    for (auto& kvp : key_val_pair) {
      if (kvp.first == "IdxOutOrderCntVal") {
        convertString(kvp.second, frmIdx);
        skipLineFlag = true;
        clockTick++;
      }
      if (!maxLevelLimit.count(kvp.first)) continue;
      if (!checkLimit(kvp.second, maxLevelLimit[kvp.first], value)) {
        cout << "\n" << kvp.first << " Value : " << value << " Exceeds Max. Limit " << maxLevelLimit[kvp.first] << endl;
        levelLimitsExceedCount_++;
        levelLimitsCount_++;
        levelLimitTestsMatch_ = false;
      }
      if (kvp.first == agrData) {
        convertString(kvp.second, tmp.data_[2]);
      }
    }
    if (!skipLineFlag) {
      totalPerSec += tmp;
      dataWindow.push_back(tmp);
      if (clockTick >= frmPerSecMin1) {
        maxValue = maxLevelLimitPerSecond[agrData];
        if (totalPerSec.data_[2] > maxValue) {
          printf(" %s MaxPerSec %zu Exceeds Table A-10 Specified Limit %zu \n", agrData.c_str(),
            totalPerSec.data_[2], maxValue);
          levelLimitTestsMatch_ = false;
        }
        totalPerSec -= dataWindow.front();
        dataWindow.pop_front();
      }
    }
  }
  levelLimitsCount_++;
}

void VMCConformance::checkACDispLevelLimits(uint8_t levelIdc, double aR, KeyValMaps& key_val_map) {
  std::map<std::string, size_t> maxLevelLimit;
  std::map<std::string, size_t> maxLevelLimitPerSecond;
  std::deque<VMCDynamicData>    dataWindow;
  std::string      agrData;
  VMCErrorMessage               error_rep;

  int64_t clockTick = -1;
  int64_t frmPerSecMin1 = (int64_t)(1 / aR) - 1;
  uint8_t levelIdx = (uint8_t)((levelIdc / 30.0 - 1) + ((levelIdc % 30) / 3.0));
  if (levelIdx >= 4) {
    error_rep.error(
      " Dynamic Conformance Check Cannot Be Done: level indicator should be in multiples of 30 i.e. 30 - 66 for "
      "levels 1 to 2.2 \n");
    levelLimitTestsMatch_ = false;
    levelLimitsCount_++;
    return;
  }
  maxLevelLimit.emplace("NumDisplacements", VDMCLevelTable[levelIdx][MaxAggregateDisplSr]);
  maxLevelLimitPerSecond.emplace("NumDisplacements", VDMCLevelTable[levelIdx][MaxAggregateDisplSr]);
  agrData = "NumDisplacements";

  // check general tier level limits A.6.1 & A.6.2
  size_t         frmIdx, value, maxValue;
  VMCDynamicData totalPerSec{};
  for (auto& key_val_pair : key_val_map) {
    VMCDynamicData tmp;
    bool           skipLineFlag = false;
    for (auto& kvp : key_val_pair) {
      if (kvp.first == "IdxOutOrderCntVal") {
        convertString(kvp.second, frmIdx);
        skipLineFlag = true;
        clockTick++;
      }
      if (!maxLevelLimit.count(kvp.first)) continue;
      if (!checkLimit(kvp.second, maxLevelLimit[kvp.first], value)) {
        cout << "\n" << kvp.first << " Value : " << value << " Exceeds Max. Limit " << maxLevelLimit[kvp.first] << endl;
        levelLimitsExceedCount_++;
        levelLimitsCount_++;
        levelLimitTestsMatch_ = false;
      }
      if (kvp.first == agrData) {
        convertString(kvp.second, tmp.data_[3]);
      }
    }
    if (!skipLineFlag) {
      totalPerSec += tmp;
      dataWindow.push_back(tmp);
      if (clockTick >= frmPerSecMin1) {
        maxValue = maxLevelLimitPerSecond[agrData];
        if (totalPerSec.data_[3] > maxValue) {
          printf(" %s MaxPerSec %zu Exceeds Table A-9 Specified Limit %zu \n", agrData.c_str(),
            totalPerSec.data_[3], maxValue);
          levelLimitTestsMatch_ = false;
        }
        totalPerSec -= dataWindow.front();
        dataWindow.pop_front();
      }
    }
  }
  levelLimitsCount_++;
}

template <typename T>
inline bool VMCConformance::checkLimit( const std::string& keyValue, T& maxVal, T& val ) {
  bool               error = true;
  std::istringstream keyVal_ss( keyValue, std::istringstream::in );
  keyVal_ss.exceptions( std::ios::failbit );
  keyVal_ss >> val;
  if ( val > maxVal ) { error = false; }
  return error;
}

template <typename T>
inline void VMCConformance::convertString( const std::string& keyValue, T& val ) {
  std::istringstream keyVal_ss( keyValue, std::istringstream::in );
  keyVal_ss.exceptions( std::ios::failbit );
  keyVal_ss >> val;
  return;
}
