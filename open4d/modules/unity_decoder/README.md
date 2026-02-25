# TVM (Time Variable Mesh) Decoder for Unity

This plugin provides a Unity playback system for playing encoded sequences using TVMC compression algorithm developed at SINRG labs. 


## Overview

This project implements a real-time decoder for compressed 3D mesh animation sequences. The plugin has two main components; a C++ dynamic
library decoder backend, and a c# Unity front end. The c++ backend handles file I/O and sequence decoding. The Unity side is responsible for
fetching and displaying the mehses in engine. Included with these files are prebuilt plugins for MacOS and Android(Quest 3) as well as the c++
backend source code if the user needs rebuild for another platform.

In addition to the main plugin there is an included script for playing unencoded TVM sequences of .obj files. Instructions for running that
script can be found at the bottom of this document.



## Project Structure

```bash
TVMCUnity/
├── CPP_Backend/                 # C++ decoder implementation
│   ├── src/
│   │   ├── core/               # Core decoder and playback management
│   │   │   ├── TVMDecoder.cpp/h
│   │   │   ├── PlaybackManager.cpp/h
│   │   │   └── TVMDecoder_Extern.cpp
│   │   ├── io/                 # I/O utilities for matrix and mesh data
│   │   │   ├── MatrixIO.cpp/h
│   │   │   └── SimpleMeshIO.cpp/h
│   │   ├── mesh/               # Mesh processing utilities
│   │   │   └── SimpleMesh.cpp/h
│   │   └── logger/             # Logging utilities
│   │       └── TVMLogger.cpp/h
│   ├── external/
│   │   └── Eigen/             # Eigen3 linear algebra library
│   └── CMakeLists.txt
├── Unity Files/                # Unity integration
│   ├── Plugins/               # Platform-specific backend builds
│   │   ├── macOS/
│   │   │   └── libTVMDecoder.dylib
│   │   └── Android/
│   │       └── arm64-v8a/
│   │           └── libTVMDecoder.so
│   └── Scripts/               # Unity C# scripts
│       ├── BasicPlayback.cs
│       ├── NonEncodedObjPlayback.cs
│       └── TVMPlaybackPlugin.cs
├── Helper_Converter_Scripts/   # Python utilities
│   ├── npy_to_bin_recursive.py
│  └── subdivider.py
├── EncodedExample/                # Encoded example sequence
│   ├── DancerSequence.zip  
```
## Features

### Core Decoder (`TVMDecoder`)
- **Efficient Decoding**: Reconstructs mesh vertices from compressed representations using:
  - Reference mesh with anchor points
  - Basis matrices for dimensionality reduction
  - Sparse matrix operations for trajectory reconstruction
- **Memory Management**: Aligned memory allocation for Eigen operations
- **Frame Access**: Random access to decoded frames within a subsequence

### Playback Manager
- **Streaming Architecture**: Manages multiple subsequences with:
  - Pre-loading window for I/O operations **NOTE: Pre-load window must be > than the decode windw!!**
  - Decode window for CPU processing
  - Thread-safe subsequence advancement
- **Asynchronous Operations**: Background loading and decoding of upcoming subsequences
- **Circular Buffer**: Automatic looping of sequences

### Unity Integration
- **Native Plugin Interface**: P/Invoke bindings for C# access
- **Mesh Reconstruction**: Real-time vertex updates and normal recalculation
- **Platform Support**: Conditional compilation for different platforms (Windows, macOS, Linux, Android, iOS)
- **Debug Logging**: Callback system for Unity console integration

### Unity Setup
1. Copy the built library to the appropriate `Unity Files/Plugins/` subfolder
2. Import the Unity scripts from `Unity Files/Scripts/`
3. Attach `BasicPlayback.cs` to a GameObject
4. Configure the sequence directory and playback parameters

## Data Format

### Encoded Sequence Structure
Sequences are expected to be provided in a zip file. The file structure should
be as follows:

```bash
Sequence/
├── subsequence_001/
    ├── ...
├── subsequence_002/
    ├── ...
├── subsequence_003/
    ├── ...
├── subsequence_...
```

**NOTE** Make sure that the parent directy was zipped with the name "Sequence", you can chage
the name of the file afterwards.

Withen each subsequence folders should be the following files:

```bash
subsequence_XXX/
├── decoded_decimated_reference_mesh_subdivided.obj # Base mesh geometry
├── anchor_indices.bin      # Fixed vertex indices
├── B_matrix.txt
├── T_matrix.txt
└── delta_trajectories.bin
```

### Encoding Your Own Sequence

It is recommended to encode larger sequences into a series of 10 frame subsrequences as demonstrated in the provided example seequence.

After running TVMC, you need to run some basic helper functions to convert the files to be usable with
our plugin. These helper files can be found in the folder TVMCUnity/Helper_Converter_Scripts/.

`npy_to_bin_recursive.py`
    This helper function is used to convert delta_trajectories.npy to a .bin format

'subdivider.py`
    This helper function is used to subdivide decoded_decimated_reference_mesh.obj to create a reference mesh
    compatible with the playback plugin.

**NOTE** All other files that were created from TVMC and **are not** listed above in the encoded sequence structure section are not required for
playback and can be deleted.


## Decoder Configuration

### Playback Parameters
- **preLoadWindow**: Number of subsequences to pre-load from disk
- **decodeWindow**: Number of subsequences to pre-decode
- **playbackFPS**: Target framerate for playback
- **enableLogging**: Toggle debug output

### Memory Optimization
Adjust window sizes based on available memory:
- Smaller windows: Lower memory usage, potential playback hiccups
- Larger windows: Smoother playback, higher memory consumption



## Unencoded Object Playback Instructions
Found at TVMCUnity//Unity Files/Scripts/NonEncodedObjPlayback.cs is a script for playing back unencoded
.obj mesh seqeunces. Usage instructions are as follows:

1. Copy TVMCUnity//Unity Files/Scripts/NonEncodedObjPlayback.cs into your Unity project.
2. Import your .obj sequence into the Unity scene. Ensure that your .obj files are numbered
   in playback order (the script sorts by object name).
3. Create a new tag in Unity to associate with your sequence and tag all sequence meshes.
3. Add NonEncodedObjPlayback.cs to a new GameObject.
4. Set your desired playback parameters and set sequenceTag to the tag set in step 3.
    

