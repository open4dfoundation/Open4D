
<!--- Building  --->
# Building 

## Building script

A bash script is provided to facilitate the building operations. 

To build V-DMC test model softwares with this script please use the following command line:

```console
$ ./build.sh
$ ./build.sh --help
./build.sh mpeg-vmesh-tm building script:

    Usage:
       -h|--help    : Display this information.
       -o|--ouptut  : Output build directory.
       -n|--ninja   : Use Ninja.
       --debug      : Build in debug mode.
       --release    : Build in release mode.
       --doc        : Build documentation (latex and pdflatex requiered).
       --format     : Format source code.
       --tidy       : Check source code with clang-tidy.
       --cppcheck   : Check source code with cppcheck.
       --test       : Build unit tests.
       --meshType=* : Define template mesh type: float or double.
       --codeCodecId: Code codec id used in the bitstream.

    Examples:
      ../build.sh
      ../build.sh --debug
      ../build.sh --doc
      ../build.sh --format
```` 

Another script could be used to clean the current solutions with the following command lines:

```console
$ ./clear.sh      # Remove ./build/ sub-folder.
$ ./clear.sh all  # Remove all cloned dependencies.
```

## Build manually

Standard CMake build commands can be used to build the 
software depending on the system you used.

### OSX
```console
$ mkdir build
$ cmake -S. -Bbuild -G Xcode
$ xcodebuild -project build/vmesh.xcodeproj -configuration Debug
```

### Linux
```console
$ mkdir build
$ cmake -DCMAKE_BUILD_TYPE=Release -S. -Bbuild/Release
$ cmake --build ./build/Release --config Release --parallel 12
```

### Windows
```console
$ md build
$ cmake -DCMAKE_BUILD_TYPE=Release -S. -Bbuild/Release
$ cmake --build ./build/Release --config Release --parallel 12
```
