# ARAP volume tracking

Time-varying meshes (TVMs), i.e., mesh sequences of varying connectivity, 
are gaining in popularity as a representation of evolving surfaces, because 
of several beneficial properties. They are comparatively easy to obtain using 
current photogrammetry methods, and they provide great versatility in comparison 
with their alternatives (such as dynamic meshes, i.e., mesh sequences with 
constant connectivity), particularly in handling topology changes. The main 
disadvantage of TVMs is the inherent lack of temporal correspondence indication, 
which hinders many practical applications, such as texturing, compression, 
and efficient processing in general.

This repository contains reference implementations for volume tracking presented at SMI2021 and ICCS 2023.
Some tweaks to the code were done after the original implementation was published. This makes the current implementation incompatible with original configuration files. See branch `smi2021` for the original version.

## IIR based affinity (SMI 2021)

In this work, we use a model for volume tracking, representing the volume elements 
by their centroid locations, called centers. Our main contribution is a 
novel definition of the energy that drives the volume tracking algorithm, 
incorporating a term that emphasizes locally rigid motion. The term builds on 
the following two coupled concepts:

1. center affinity, expressed in terms of the local similarity of the rigid transformations describing the movement of a center and its neighborhood, and

2. a local measure of motion rigidity, expressed as the deviation from motion that is locally as rigid as possible.

Reference implementation of the method presented in [As-rigid-as-possible 
volume tracking for time-varying surfaces (SMI 2021)](https://doi.org/10.1016/j.cag.2021.10.015)
. 

## Max-based affinity and Global optimisation (ICCS 2023)

In this work, we address the issues of the IIR based implementation by proposing an improved tracking procedure, which efficiently eliminates most of the problems encountered previously. Our main contributions are:

1. an improved approach to evaluating volume element affinity, which eliminates reconnecting of previously separated components caused by the infinite impulse response (IIR) filter used in the state of the art,
2. a robust measure capable of identifying incorrectly tracked volume elements, which allows removing them from the intermediate result,
3. a new post-processing phase that optimises tracking criteria globally, taking the whole sequence into account, allowing temporal propagation of information in both directions. 

Reference implementation of the improvements presented in [Global Optimisation for Improved Volume Tracking of Time-Varying Meshes (ICCS 2023)](https://doi.org/10.1007/978-3-031-36027-5_9).


## BibTeX

```
% IIR based affinity
@article{ArapVolumeTracking,
	title = {As-rigid-as-possible volume tracking for time-varying surfaces},
	journal = {Computers & Graphics},
	volume = {102},
	pages = {329-338},
	year = {2022},
	issn = {0097-8493},
	doi = {10.1016/j.cag.2021.10.015},
	author = {Jan Dvořák and Zuzana Káčereková and Petr Vaněček and Lukáš Hruda and Libor Váša}
}

% Max-based affinity and Global optimisation
@InCollection{MaxAffinityGlobalTracking,
  author    = {Jan Dvo{\v{r}}{\'{a}}k and Filip H{\'{a}}cha and Libor V{\'{a}}{\v{s}}a},
  booktitle = {Computational Science {\textendash} {ICCS} 2023},
  publisher = {Springer Nature Switzerland},
  title     = {Global Optimisation for~Improved Volume Tracking of~Time-Varying Meshes},
  year      = {2023},
  pages     = {113--127},
  doi       = {10.1007/978-3-031-36027-5_9},
}
```

## Build

The application is written in C# for .NET 7. You can build the project 
with dotnet tools: 

```
dotnet build -c release
```
The build application is in `./bin` folder

## Run
To run the process, execute `dotnet ./bin/client.dll <config_file.xml>` (or `./bin/client.exe <config_file.xml>` on Windows OS) with a configuration file. 

### Sample configuration file for IIR affinity based tracking

```xml
<?xml version="1.0"?>
<Config xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <mode>process IIR</mode>
  <firstIndex>0</firstIndex>
  <lastIndex>59</lastIndex>
  <inDir>data\collision</inDir>
  <fileNamePrefix>mesh_0</fileNamePrefix>
  <outDir>data\collision-output-iir</outDir>
  <volumeGridResolution>512</volumeGridResolution>
  <pointCount>2000</pointCount>
  <gradientThreshold>0.0001</gradientThreshold>
  <smoothSigma>0.1816</smoothSigma>
  <smoothSigma2>0.01</smoothSigma2>
  <falloffStrength>0.05</falloffStrength>
  <applySmooth>1</applySmooth>
  <applyLloyd>1</applyLloyd>
</Config>
```

### Sample configuration file for Max affinity based tracking

```xml
<?xml version="1.0"?>
<Config xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <firstIndex>0</firstIndex>
  <lastIndex>59</lastIndex>
  <inDir>data\collision</inDir>
  <fileNamePrefix>mesh_0</fileNamePrefix>
  <outDir>data\collision-output</outDir>
  <volumeGridResolution>512</volumeGridResolution>
  <pointCount>2000</pointCount>
  <gradientThreshold>0.0001</gradientThreshold>
  <smoothSigma>0.125</smoothSigma>
  <smoothSigma2>0.125</smoothSigma2>
  <falloffStrength>0.05</falloffStrength>
  <applySmooth>1</applySmooth>
  <applyLloyd>1</applyLloyd>
</Config>
```

### Sample configuration file for Global optimisation (GO)

```xml
<?xml version="1.0"?>
<Config xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <mode>improvement</mode>
  <firstIndex>0</firstIndex>
  <lastIndex>59</lastIndex>
  <inDir>data\collision</inDir>
  <fileNamePrefix>mesh_0</fileNamePrefix>
  <outDir>data\collision-output</outDir>
  <volumeGridResolution>512</volumeGridResolution>
  <pointCount>2000</pointCount>
  <gradientThreshold>0.0001</gradientThreshold>
  <smoothSigma>0.125</smoothSigma>
  <smoothSigma2>0.03</smoothSigma2>
  <applySmooth>1</applySmooth>
  <applyLloyd>0.5</applyLloyd>
  <filterCount>5</filterCount>
  <numberOfImprovements>4</numberOfImprovements>
  <maxIt>30</maxIt>
</Config>
```

### Configuration parameter reference

- mode Tracking mode
	* ```process IIR``` - IIR affinity based tracking
	* ```process``` or unspecified - Max afinity based tracking
	* ```improvement``` - Global optimisation (requires first running any of the forward tracking modes)
- inDir - directory with input data
- fileNamePrefix - the name of data files excluding the last 3 numbers (eg. mesh_0)
- firstIndex/lastIndex - number of the first/last file (eg. for 0, the first file is mesh_0000.obj)
- outDir - directory for output files
- volumeGridResolution - the resolution of volume grid in the largest direction for data processing
- pointCount - the number of tracked point
- gradientThreshold - threshold for gradient element size for stopping optimization
- smoothSigma - controls falloff for distance based affinity
- smoothSigma2 - controls falloff transformation difference based affinity
- falloffStrength - controls IIR filter
- applySmooth - weight for smoothness term
- applyLloyd - weight for uniformness term
- filterCount - number of centers to be removed each improvement (GO mode only)
- numberOfImprovements - number of improvement attempts (GO mode only)
- maxIt - maximum number of iterations each improvement (GO mode only)
