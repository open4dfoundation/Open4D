
<!--- Usage  --->
# Usage

## Encode 

The encode command line is the following one: 

```console
$ ./build/Release/bin/encode \
    --config=./generatedConfigFiles/s3c1r3_bask/encoder.cfg \
    --frameCount=1 \
    --compressed=s3c1r3_bask.vmesh 
```

## Decode

The decode can be executed with:

```console
./build/Release/bin/decode \
  --config=./generatedConfigFiles/s3c1r3_bask/decoder.cfg \
  --compressed=s3c1r3_bask.vmesh \
  --decMesh=s3c1r3_bask_%04d_dec.obj \
  --decTex=s3c1r3_bask_%04d_dec.png \
  --decMat=s3c1r3_bask_%04d_dec.mtl \
```

## Runtime configuration and configuration files

To generate the configuration files (conmon test conditions) according to your system paths, the following action must be made: 

1. copy and edit `cfg/cfg-site-default.yaml` as `cfg/cfg-site.yaml`,
   the paths for the binaries, sequence prefix, and the external
   tool configuration prefix;

2. run the ./scripts/gen-cfg.sh' script:

```console
$ ./scripts/gen-cfg.sh \
    --cfgdir=./cfg/ \
    --outdir=/path/to/generated/cfgfiles
```

This operation can be executed with script './scripts/create_configuration_files.sh' and in this case the file 'cfg/cfg-site.yaml' is generated automatically according to the current folder.

```console

$ ./scripts/create_configuration_files.sh 
. /scripts/create_configuration_files.sh Create configuration files:

  Usage:
    -o|--outdir=: configured directory      (default: config/ )
    -s|--seqdir=: source sequence directory (default:  )
    -c|--codec=:  video codec: hm, vtm      (default: hm )

  Examples:
    ./scripts/create_configuration_files.sh
    ./scripts/create_configuration_files.sh \
      --outdir=generatedConfigFilesHM  \
      --seqdir=/path/to/contents/voxelized/ \
      --codec=hm
    ./scripts/create_configuration_files.sh \
      --outdir=generatedConfigFilesVTM \
      --seqdir=/path/to/contents/voxelized/ \
      --codec=vtm

```

## Run experiment

An example script (`scripts/run.sh`) demonstrates how
to launch the entire toolchain for a single job in the configured experiment. 

This scripts starts:

- encoding process
- decoding process
- pcc metrics computation
- ibsm metrics computation

The usage of this script are presented below: 

```console
$ ./scripts/run.sh 

    Usage:
       -h|--help   : print help
       -q|--quiet  : disable logs            (default: 1 )
       -f|--frames : frame count             (default: 1 )
       -c|--cfgdir : configured directory    (default: "" )
       -o|--outdir : output directory        (default: "results" )
       --condId=   : condition: 1, 2         (default: 1 )
       --seqId=    : seq: 1,2,3,4,5,6,7,8    (default: 1 )
       --rateId=   : Rate: 1,2,3,4,5         (default: 1 )
       --tmmMetric : Use TMM metric software (default: 0 )
       --render    : Create rendered images  (default: 0 )
       --encParams : configured directory    (default: "" )
       --decParams : configured directory    (default: "" )
       --csv       : generate .csv file      (default: "" )

    Examples:
      - ../scripts/run.sh
      - ../scripts/run.sh \
          --condId=1 \
          --seqId=3 \
          --rateId=3 \
          --cfgdir=generatedConfigFiles
      - ../scripts/run.sh \
          --condId=1 \
          --seqId=3 \
          --rateId=3 \
          --cfgdir=generatedConfigFiles \
          --TMMMETRIC

```

Note: The preceding script uses the `mpeg-pcc-mmetric` software and this dependency can be cloned and built with the following command line:

```console
$ ./scripts/get_external_tools.sh 
```

A example of execution of this script is:

```console
$ ./scripts/run.sh \
    --condId=1 \
    --seqId=3 \
    --rateId=3 \
    --cfgdir=generatedConfigFiles \
    --outdir=results
Run vmesh encoder/decoder/metrics: ./scripts
Encode: results/F001/s3c1r2_bask/s3c1r2_bask
./build/Release/bin/encode \
    --config=./generatedConfigFiles/s3c1r2_bask//encoder.cfg \
    --frameCount=1 \
    --compressed=results/F001/s3c1r2_bask/s3c1r2_bask.vmesh \
   > results/F001/s3c1r2_bask/encoder.log 2>&1
Decode: results/F001/s3c1r2_bask/s3c1r2_bask
./build/Release/bin/decode \
    --config=./generatedConfigFiles/s3c1r2_bask//decoder.cfg \
    --compressed=results/F001/s3c1r2_bask/s3c1r2_bask.vmesh \
    --decMesh=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.obj \
    --decTex=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.png \
    --decMat=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.mtl \
   > results/F001/s3c1r2_bask/decoder.log 2>&1
Metrics IBSM: results/F001/s3c1r2_bask/s3c1r2_bask
./externaltools/mpeg-pcc-mmetric/build/mm \
   sequence \
    --firstFrame    1 \
    --lastFrame     1 \
   END \
   dequantize \
    --inputModel    /path/to/contents/basketball_player_fr%04d_qp12_qt12.obj \
    --outputModel   ID:deqRef \
    --useFixedPoint \
    --qp            12 \
    --minPos        "-725.812988 -483.908997 -586.02002" \
    --maxPos        "1252.02002 1411.98999 1025.34998" \
    --qt            12 \
    --minUv         "0 0" \
    --maxUv         "1.0 1.0" \
   END \
   dequantize \
    --inputModel    results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.obj \
    --outputModel   ID:deqDis \
    --useFixedPoint \
    --qp            12 \
    --minPos        "-725.812988 -483.908997 -586.02002" \
    --maxPos        "1252.02002 1411.98999 1025.34998" \
    --qt            12 \
    --minUv         "0 0" \
    --maxUv         "1.0 1.0" \
   END \
   compare \
    --mode          ibsm \
    --inputModelA   ID:deqRef \
    --inputModelB   ID:deqDis \
    --inputMapA     /path/to/contents/basketball_player_fr%04d.png \
    --inputMapB     results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.png \
    --outputCsv     results/F001/s3c1r2_bask/metric_ibsm.csv \
   > results/F001/s3c1r2_bask/metric_ibsm.log
Metrics PCC: results/F001/s3c1r2_bask/s3c1r2_bask
./externaltools/mpeg-pcc-mmetric/build/mm \
   sequence \
    --firstFrame    1 \
    --lastFrame     1 \
   END \
   dequantize \
    --inputModel    /path/to/contents/basketball_player_fr%04d_qp12_qt12.obj \
    --outputModel   ID:deqRef \
    --useFixedPoint \
    --qp            12 \
    --qt            12 \
    --minPos        "-725.812988 -483.908997 -586.02002" \
    --maxPos        "1252.02002 1411.98999 1025.34998" \
    --minUv         "0.0 0.0" \
    --maxUv         "1.0 1.0" \
   END \
   dequantize \
    --inputModel    results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.obj \
    --outputModel   ID:deqDis \
    --useFixedPoint \
    --qp            12 \
    --qt            12 \
    --minPos        "-725.812988 -483.908997 -586.02002" \
    --maxPos        "1252.02002 1411.98999 1025.34998" \
    --minUv         "0.0 0.0" \
    --maxUv         "1.0 1.0" \
   END \
   reindex \
    --inputModel    ID:deqRef \
    --sort          oriented \
    --outputModel   ID:ref_reordered \
   END \
   reindex \
    --inputModel    ID:deqDis \
    --sort          oriented \
    --outputModel   ID:dis_reordered \
   END \
   sample \
    --inputModel    ID:ref_reordered \
    --inputMap      /path/to/contents/basketball_player_fr%04d.png \
    --mode          grid \
    --useNormal \
    --useFixedPoint \
    --minPos        "-725.812988 -483.908997 -586.02002" \
    --maxPos        "1252.02002 1411.98999 1025.34998" \
    --bilinear \
    --gridSize      1024 \
    --hideProgress  1 \
    --outputModel   ID:ref_pc \
   END \
   sample \
    --inputModel    ID:dis_reordered \
    --inputMap      results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.png \
    --mode          grid \
    --useNormal \
    --useFixedPoint \
    --minPos        "-725.812988 -483.908997 -586.02002" \
    --maxPos        "1252.02002 1411.98999 1025.34998" \
    --bilinear \
    --gridSize      1024 \
    --hideProgress  1 \
    --outputModel   ID:dis_pc \
   END \
   compare \
    --mode          pcc \
    --inputModelA   ID:ref_pc \
    --inputModelB   ID:dis_pc \
    --resolution    1977.833008 \
    --outputCsv     results/F001/s3c1r2_bask/metric_pcc.csv \
   > results/F001/s3c1r2_bask/metric_pcc.log
NbOutputFaces      : 75648
TotalBitstreamBits : 150448
GridD1             : 73.833939
GridD2             : 75.434639
GridLuma           : 36.139542
GridChromaCb       : 43.351307
GridChromaCr       : 45.376358
IbsmGeom           : 46.775490
IbsmLuma           : 33.573721
EncTime            : 27.0134349
DecTime            : 0.34059222

```

The --tmmMetric paramerter executes the vmesh metric software to compute metrics rather than the mm software. 
In this case, the logs are as follows: 

```console
$ ./scripts/run.sh \
  --condId=1 \
  --seqId=3 \
  --rateId=2 \
  --cfgdir=generatedConfigFiles \
  --outdir=results \
  --tmmMetric
Encode: results/F001/s3c1r2_bask/s3c1r2_bask
./build/Release/bin/encode \
    --config=./generatedConfigFiles/s3c1r2_bask//encoder.cfg \
    --frameCount=1 \
    --compressed=results/F001/s3c1r2_bask/s3c1r2_bask.vmesh \
   > results/F001/s3c1r2_bask/encoder.log 2>&1
Decode: results/F001/s3c1r2_bask/s3c1r2_bask
./build/Release/bin/decode \
    --config=./generatedConfigFiles/s3c1r2_bask//decoder.cfg \
    --compressed=results/F001/s3c1r2_bask/s3c1r2_bask.vmesh \
    --decMesh=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.obj \
    --decTex=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.png \
    --decMat=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.mtl \
   > results/F001/s3c1r2_bask/decoder.log 2>&1
Metrics: results/F001/s3c1r2_bask/s3c1r2_bask
./build/Release/bin/metrics \
    --config=./generatedConfigFiles/s3c1r2_bask//mmetric.cfg \
    --decMesh=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.obj \
    --decTex=results/F001/s3c1r2_bask/s3c1r2_bask_%04d_dec.png \
    --frameCount=1 \
   > results/F001/s3c1r2_bask/metric_met.log
NbOutputFaces      : 75648
TotalBitstreamBits : 150448
GridD1             : 73.8339386
GridD2             : 75.434639
GridLuma           : 36.1395416
GridChromaCb       : 43.3513069
GridChromaCr       : 45.376358
IbsmGeom           : 46.7754899
IbsmLuma           : 33.5737214
EncTime            : 28.7190478
DecTime            : 0.355798779
```

## Collect results

To collect the results from the log files (encoder, decoder and metric), the `./scripts/collect_results.sh` script can be uses:

```console
$ ./scripts/collect_results.sh
./scripts/collect_results.sh Collect results from log files

  Usage:
    -h|--help   : print help
    -q|--quiet  : disable logs         (default: 1 )
    --condId=   : condition: 1, 2      (default: 1 )
    --seqId=    : seq: 1,2,3,4,5,6,7,8 (default: 1 )
    --rateId=   : Rate: 1,2,3,4,5      (default: 1 )
    --vdmc      : vdmc bitstream file  (default: "" )
    --logenc    : encoder log file     (default: "" )
    --logdec    : decoder log file     (default: "" )
    --logmet    : metrics log file     (default: "" )
    --csv       : generate .csv file   (default: "" )

  Examples:
    ./scripts/collect_results.sh -h
    ./scripts/collect_results.sh \
      --condId=1 \
      --seqId=3 \
      --rateId=3 \
      --vdmc=test.bin \
      --logenc=encoder.log \
      --logdec=decoder.log \
      --logmet=metric.log
```

This script can be used to parse the log files and display or get the bitrate/metric values:

```console
$ ./scripts/collect_results.sh  \
  --condId  1 \
  --seqId   2 \
  --rateId  2 \
  --vdmc    s2c1r2_sold.vmesh \
  --logenc  encoder.log     \
  --logdec  decoder.log     \
  --logmet  metrics.log
2,1,2,152064,206976,72.1250986,74.0027083,29.7090586,43.1735896,43.2807054,\
48.3653428,29.1616112,24.8878219,0.257652824,484.457,178.211
$ RES=( $( ./scripts/collect_results.sh  \
  --condId  1 \
  --seqId   2 \
  --rateId  2 \
  --vdmc    s2c1r2_sold.vmesh \
  --logenc  encoder.log     \
  --logdec  decoder.log     \
  --logmet  metrics.log ) ); 
$ for((i=0;i<16;i++)); do printf "RES[%2d] = %s \n" $i ${RES[$i]}; done
RES[ 0] = 2
RES[ 1] = 1
RES[ 2] = 2
RES[ 3] = 152064
RES[ 4] = 206976
RES[ 5] = 72.1250986
RES[ 6] = 74.0027083
RES[ 7] = 29.7090586
RES[ 8] = 43.1735896
RES[ 9] = 43.2807054
RES[10] = 48.3653428
RES[11] = 29.1616112
RES[12] = 24.8878219
RES[13] = 0.257652824
RES[14] = 484.457
RES[15] = 178.211
```

## Run all experiments and create render and graph pdf files

To run CTC experiments with all sequences, all conditions and all rates as definied in CTC conditions, the `./scripts/run_all.sh`script can be used :

```console
$ ./scripts/run_all.sh --help
./scripts/run_all.sh execute all encoding/decoding/metrics
  Usage:
    -h|--help    : print help
    -q|--quiet   : disable logs                   (default: 1 )
    -f|--frames  : frame count                    (default: 1 )
    -c|--cfgdir  : configured directory           (default: "config" )
    -o|--outdir  : output directory               (default: tests )
    --experiments: csv configuration files        (default: test.csv )
    --tmmMetric  : Use TMM metric software        (default: 0 )
    -t|--threads : Number of parallel experiments (default: 1 )
    --render     : Create pdf with rendered images(default: 0 )
    --graph      : Create pdf with metric graphs  (default: 0 )
    --xlsm       : Create CTC xlsm files          (default: 0 )

  Examples:
    ./scripts/run_all.sh -h
    ./scripts/run_all.sh \
      --experiments ./scripts/test.csv \
      --outdir      experiments \
      --cfgdir      generatedConfigFilesHM \
      --frame       2 \
      --graph \
      --render \
      --xlsm \
      --quiet
```

This scripts executes severals experiments that must be defined in `./scripts/test.csv` files. This file defined the experiments that must be evaluated, one experiments by line. Each experiments must set:

- Name: the name of the experiment.
- EncParams: the encoder parameters used.
- DecParams: the decoder parameters used.

An example of this file is the following one:

```console
$ cat ./scripts/test.csv
Name,EncParams,DecParams
anchor,,
texture1k,--textureVideoWidth=1024 --textureVideoHeight=1024,
texture2k,--textureVideoWidth=2048 --textureVideoHeight=2048,
```

The experiments can be executed with the following command line:

```console
$ ./scripts/run_all.sh \
  --frame=4 \
  --threads 10 \
  --render \
  --graph \
  --xlsm \
  --quiet
```

The `--graph` and `--renderer`  options create pdf files with the graph and the render images of all the experiments defined in `./scripts/test.csv`. Examples of the created pdf files can be seen in figures 2 and 3. 

![Example of graphs.](images/graph.png){width=520 height=400}

![Example of render images.](images/render.png){width=520 height=400} 

The `--xlsm` option fill the CTC XLSM spreadsheet with the results of the current experiences. The first line of the CSV file is set as anchor  of the experiences and the other one are compared to the anchor and between them. With the previously presented CSV files teh following files are created: 

- ./experiments/F004_anchor_vs_texture1k.xlsm 
- ./experiments/F004_anchor_vs_texture2k.xlsm
- ./experiments/F004_texture1k_vs_texture2k.xlsm

Note: The `--xlsm` option uses `openpyxl` Python module to fill the XLSM files and requieres Python3 to work properly.  

The `--threads N` option allows experiments to be run in parallel with N which defines the number of parallel tests. 

Note: This option has been used on Linux and uses Linux commands to work. Please, use this script in a Linux terminal. On Window, please uses: msys, cygwin, mingw or Windows Subsystem for Linux (WSL).

## View decoded sequences

The subjective quality of the decoded sequences can be evaluated by playing the decoded .ply/.png files with the mpeg-pcc-renderer (http://mpegx.int-evry.fr/software/MPEG/PCC/mpeg-pcc-renderer.git).

The following commands can be used to install and to execute this software:

```console
git clone http://mpegx.int-evry.fr/software/MPEG/PCC/mpeg-pcc-renderer.git
cd mpeg-pcc-renderer/
./build.sh
./bin/windows/Release/PccAppRenderer.exe \
  -f ./s1c1r1_long/s1c1r1_long_dec_fr1051.ply
```

A specific script can be used to create video of the decoded sequences like shown in figure 3:

```console
./scripts/renderer.sh \
  -i ./s1c1r1_long/  \
  --videoType=4   \
  -w 600 \
  -h 800 \
  --cameraPathIndex=10
  
```

![Screenshoot of the mpeg-pcc-renderer software.](images/renderer.png){width=520 height=400}