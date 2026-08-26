
# program_options_lite 
add_library(po STATIC "${PROJECT_SOURCE_DIR}/dependencies/program-options-lite/program_options_lite.cpp" )
target_include_directories(po INTERFACE "${PROJECT_SOURCE_DIR}/dependencies/" )

########################### Main applications ###########################

# Encode
create_executable( NAME      encode 
                   SOURCES encodeApp.cpp
                   LIBRARIES po vmeshCommon vmeshEncoder v3cBitstream UVAtlas DirectX-Headers DirectXMath DirectXMesh 
                   INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
                             ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
                             ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                             ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon/util
                             ${CMAKE_SOURCE_DIR}/source/lib/vmeshEncoder
                             ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )  

# Decode
create_executable( NAME      decode 
                   SOURCES   decode.cpp 
                   LIBRARIES po vmeshCommon vmeshDecoder v3cBitstream atlasBitstream
                   INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream
                            ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream
                            ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream/syntaxElements
                             ${CMAKE_SOURCE_DIR}/source/lib/atlasCommon
                             ${CMAKE_SOURCE_DIR}/source/lib/atlasDecoder
                             ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
                             ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
                             ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                             ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon/util
                             ${CMAKE_SOURCE_DIR}/source/lib/vmeshDecoder
                             ${CMAKE_SOURCE_DIR}/source/lib/vmeshConformance
                             ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

# Metrics
create_executable( NAME      metrics 
                   SOURCES   metrics.cpp 
                   LIBRARIES po vmeshCommon
                   INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                             ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

# Test BaseMesh Encoder
create_executable( NAME      baseMeshEncodeApp
        SOURCES wrapper/baseMeshEncodeApp.cpp
        LIBRARIES po vmeshCommon vmeshEncoder  UVAtlas DirectX-Headers DirectXMath DirectXMesh
        INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon/util
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshEncoder
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshCommon
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshEncoder
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshEncoder
        ${CMAKE_SOURCE_DIR}/source/wrapper
        ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

# Test BaseMesh Decoder
create_executable( NAME      baseMeshDecodeApp
        SOURCES wrapper/baseMeshDecodeApp.cpp
        LIBRARIES po vmeshCommon vmeshDecoder basemeshDecoder
        INCLUDES ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/basemeshDecoder
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon/util
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshDecoder
        ${CMAKE_SOURCE_DIR}/source/lib/vmeshConformance
        ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

# Test AC Displacement Encoder
create_executable( NAME      acdEncodeApp
        SOURCES wrapper/acdEncodeApp.cpp
        LIBRARIES po acDisplacementBitstream bitstreamsCommon
        INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementCommon
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementEncoder
        ${CMAKE_SOURCE_DIR}/source/wrapper )

# Test AC Displacement Decoder
create_executable( NAME      acdDecodeApp
        SOURCES wrapper/acdDecodeApp.cpp
        LIBRARIES po vmeshCommon vmeshEncoder v3cBitstream acDisplacementBitstream
        INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementCommon
        ${CMAKE_SOURCE_DIR}/source/lib/acDisplacementEncoder
        ${CMAKE_SOURCE_DIR}/source/wrapper)

# Test Atlas Encoder
create_executable( NAME      atlasEncodeApp
        SOURCES wrapper/atlasEncodeApp.cpp
        LIBRARIES po atlasEncoder atlasBitstream bitstreamsCommon
        INCLUDES
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/atlasCommon
        ${CMAKE_SOURCE_DIR}/source/lib/atlasEncoder
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon
        ${CMAKE_SOURCE_DIR}/source/lib/bitstreamsCommon/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/wrapper )

# Test Atlas Decoder
create_executable( NAME      atlasDecodeApp
        SOURCES wrapper/atlasDecodeApp.cpp
        LIBRARIES po
        INCLUDES
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream/syntaxElements
        ${CMAKE_SOURCE_DIR}/source/lib/atlasBitstream
        ${CMAKE_SOURCE_DIR}/source/lib/atlasCommon
        ${CMAKE_SOURCE_DIR}/source/lib/atlasEncoder
        ${CMAKE_SOURCE_DIR}/source/wrapper)

########################### Unit tests ###########################

if ( BUILD_WRAPPER_APPS )
  # ply
  create_executable( NAME      objToPlyConvert
                     SOURCES   wrapper/objToPlyConvert.cpp 
                     LIBRARIES po vmeshCommon 
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                               ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon/util
                               ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

  # Draco Encoder/decoder
  create_executable( NAME      decodeDraco 
                     SOURCES   wrapper/dracoDecoder.cpp 
                     LIBRARIES po geometryDecoder v3cBitstream bitstreamsCommon
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                               ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
                               ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
                               ${CMAKE_SOURCE_DIR}/source/wrapper/geometryDecoder
                               ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

  # EB Encoder/decoder
  create_executable( NAME      ebEncode
                     SOURCES   wrapper/ebEncode.cpp 
                     LIBRARIES ebCommon ebEncoder ebBitstream
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/ebBitstream
                               ${CMAKE_SOURCE_DIR}/source/lib/ebCommon
                               ${CMAKE_SOURCE_DIR}/source/lib/ebEncoder
                               ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies
                               ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

  create_executable( NAME      ebDecode
                     SOURCES   wrapper/ebDecode.cpp 
                     LIBRARIES ebCommon ebDecoder
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/ebCommon
                               ${CMAKE_SOURCE_DIR}/source/lib/ebDecoder
                               ${CMAKE_SOURCE_DIR}/dependencies/mmetric/dependencies
                               ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )

  # Video Encoder/decoder
  create_executable( NAME      encodeVideo 
                     SOURCES   wrapper/videoEncoder.cpp 
                     LIBRARIES po videoEncoder v3cBitstream bitstreamsCommon
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                               ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
                               ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
                               ${CMAKE_SOURCE_DIR}/source/wrapper/videoEncoder )

  create_executable( NAME      decodeVideo 
                     SOURCES   wrapper/videoDecoder.cpp 
                     LIBRARIES po videoDecoder v3cBitstream bitstreamsCommon
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                               ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream
                               ${CMAKE_SOURCE_DIR}/source/lib/v3cBitstream/syntaxElements
                               ${CMAKE_SOURCE_DIR}/source/wrapper/videoDecoder )

  # Colour Converter
  create_executable( NAME      colourConverte
                     SOURCES   wrapper/colourConverter.cpp 
                     LIBRARIES po colourConverter
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                               ${CMAKE_SOURCE_DIR}/source/wrapper/colourConverter )
  
  # Checksum
  create_executable( NAME      checksum
                     SOURCES   wrapper/checksum.cpp 
                     LIBRARIES po vmeshCommon
                     INCLUDES  ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon )                            
endif()
                                                        
########################### Test applications ###########################
if( BUILD_UNIT_TEST_APPS )
  CPMAddPackage( "gh:google/googletest#release-1.11.0" )
  enable_testing()

  create_executable( NAME      unitTests 
                     SOURCES   unitTests/main.cpp
                               unitTests/draco.cpp 
                               unitTests/metrics.cpp
                               unitTests/hm.cpp
                               unitTests/colourConvert.cpp
                               unitTests/vmesh.cpp
                     LIBRARIES gtest_main 
                               po
                               vmeshCommon 
                               vmeshEncoder 
                               vmeshDecoder 
                               geometryEncoder 
                               geometryDecoder 
                               videoEncoder 
                               videoDecoder 
                               colourConverter
                               UVAtlas 
                               DirectX-Headers 
                               DirectXMath 
                               DirectXMesh
                     INCLUDES  ${CMAKE_SOURCE_DIR}/dependencies
                               ${CMAKE_SOURCE_DIR}/source/lib/vmeshCommon
                               ${CMAKE_SOURCE_DIR}/source/lib/vmeshEncoder 
                               ${CMAKE_SOURCE_DIR}/source/lib/vmeshDecoder 
                               ${CMAKE_SOURCE_DIR}/source/wrapper/geometryEncoder
                               ${CMAKE_SOURCE_DIR}/source/wrapper/geometryDecoder
                               ${CMAKE_SOURCE_DIR}/dependencies/tinyply/source )
  include(GoogleTest)
  gtest_discover_tests( unitTests )
endif()


