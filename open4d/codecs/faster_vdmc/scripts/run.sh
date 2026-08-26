#!/bin/bash

CURDIR=$( cd "$( dirname "$0" )" && pwd );
MAINDIR=$( dirname ${CURDIR} )

function formatCmd(){
  local f=${1};
  for s in ${2} ; do
    if [ $s == "--" ] ; then f=${f//${s}/ \\\\\\n    ${s}};  else f=${f// ${s}/ \\\\\\n   ${s}};  fi
  done
  echo -e "$f" | sed 's/ *\\$/ \\/'
}
function clip() { awk -v v="$1" -v a="$2" -v b="$3" 'BEGIN {print (v<a?a:v>b?b:v)}'; }
function isNotFinishTMM(){ [ ! -f "$1" ] || [[ "$( cat $1 | grep 'All frames have been'        )" == "" ]] && return; false; }
function isNotFinishMM() { [ ! -f "$1" ] || [[ "$( cat $1 | grep 'Time on overall processing:' )" == "" ]] && return; false; }

function setSoftwarePath(){
  ENCODER=${MAINDIR}/build/Release/bin/encode
  DECODER=${MAINDIR}/build/Release/bin/decode
  MMETRIC=${MAINDIR}/externaltools/mpeg-pcc-mmetric/build/Release/bin/mm
  RENDERER=${MAINDIR}/externaltools/mpeg-pcc-renderer/bin/linux/Release/PccAppRenderer
  if [ ! -f "${ENCODER}"  ] ; then ENCODER=${MAINDIR}/build/Release/bin/Release/encode.exe; fi
  if [ ! -f "${DECODER}"  ] ; then DECODER=${MAINDIR}/build/Release/bin/Release/decode.exe; fi
  if [ ! -f "${MMETRIC}"  ] ; then MMETRIC=${MAINDIR}/externaltools/mpeg-pcc-mmetric/build/Release/bin/Release/mm.exe; fi
  if [ ! -f "${RENDERER}" ] ; then RENDERER=${MAINDIR}/externaltools/mpeg-pcc-renderer/bin/windows/Release/PccAppRenderer.exe; fi
  if [ ${TMMMETRIC} == 1 ] ; then
    METRICS=${MAINDIR}/build/Release/bin/metrics
    if [ ! -f "${METRICSgit }" ] ; then METRICS=${MAINDIR}/build/Release/bin/Release/metrics.exe; fi
    if [ ! -f "${METRICS}" ] ; then print_usage "${METRICS} not exist, please run ./build.sh"; fi
  fi
  for NAME in $ENCODER $DECODER $MMETRIC $RENDERER; do
    if [ ! -f $NAME ] ; then
      print_usage "$NAME not exist, please run ./build.sh";
    fi
  done
  if [ $VIDEO == 1 ] ; then
    if ! command -v ${FFMPEG} &> /dev/null ; then
      if [ ! -f "${FFMPEG}" ] ; then
        print_usage "${FFMPEG} not exist, please add \--ffmpeg path_to_ffmpeg\" ";
      fi
    fi
  fi
}

function encode() {
  if isNotFinishTMM ${LOGENC} ;
  then
    CMD="$ENCODER \
      --config=${CFGSUBDIR}/encoder.cfg \
      --frameCount=${FRAMECOUNT} \
      --compressed=${VDMC} \
      ${ENCPARAMS[@]} \
      > ${LOGENC} 2>&1"
    if (( $VERBOSE )) ; then
      echo -e "\033[0;32mEncode: ${NAME} \033[0m";
      formatCmd "$CMD" "-- >"
    fi
    if ! eval $CMD ; then echo "ERROR: encode sw return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${LOGENC} already exist"; fi
  fi
}

function decode() {
  if isNotFinishTMM ${LOGDEC} ;
  then
    if [ ${CONDID} == 0 ] ; then
     CMD="$DECODER \
       --config=${CFGSUBDIR}/decoder.cfg \
       --compressed=${VDMC} \
       --decMesh=${NAME}_%04d_dec_raw.obj \
       --decTex=${NAME}_%04d_dec.png \
       --decMat=${NAME}_%04d_dec.mtl \
       ${DECPARAMS[@]} \
       > ${LOGDEC} 2>&1 ";
	  else
	    CMD="$DECODER \
      --config=${CFGSUBDIR}/decoder.cfg \
      --compressed=${VDMC} \
      --decMesh=${NAME}_%04d_dec_raw.ply \
      --decTex=${NAME}_%04d_dec.png \
      --decMat=${NAME}_%04d_dec.mtl \
      ${DECPARAMS[@]} \
      > ${LOGDEC} 2>&1 ";
	  fi
    if (( $VERBOSE )) ; then
      echo -e "\033[0;32mDecode: ${NAME} \033[0m";
      formatCmd "$CMD" "-- >"
    fi
    if ! eval $CMD ; then echo "ERROR: decode sw return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${LOGDEC} already exist"; fi
  fi
}

function metrics() {
  if isNotFinishTMM ${LOGMET} ;
  then
    if [ ${CONDID} == 0 ] ; then
    CMD="${METRICS} \
      --config=${CFGSUBDIR}/mmetric.cfg \
      --decMesh=${NAME}_%04d_dec_raw.obj \
      --decTex=${NAME}_%04d_dec.png \
      --frameCount=${FRAMECOUNT} \
      > ${LOGMET}";
    else
    CMD="${METRICS} \
      --config=${CFGSUBDIR}/mmetric.cfg \
      --decMesh=${NAME}_%04d_dec_raw.ply \
      --decTex=${NAME}_%04d_dec.png \
      --frameCount=${FRAMECOUNT} \
      > ${LOGMET}";
    fi
    if (( $VERBOSE )) ; then
      echo -e "\033[0;32mMetrics: ${NAME} \033[0m";
      formatCmd "$CMD" "-- >"
    fi
    if ! eval $CMD ; then echo "ERROR: metrics sw return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${LOGMET} already exist"; fi
  fi
}

function mmetric() {
  if isNotFinishMM ${LOGMET} ;
  then
    START=$(       cat ${CFGSUBDIR}/mmetric.cfg | grep "startFrameIndex:"  | awk '{print $2}' )
    SRCMESH=$(     cat ${CFGSUBDIR}/mmetric.cfg | grep "srcMesh:"          | awk '{print $2}' )
    QP=$(          cat ${CFGSUBDIR}/mmetric.cfg | grep "positionBitDepth:" | awk '{print $2}' )
    QT=$(          cat ${CFGSUBDIR}/mmetric.cfg | grep "texCoordBitDepth:" | awk '{print $2}' )
    MINPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "minPosition:"      | awk '{print $2" "$3" "$4}' )
    MAXPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "maxPosition:"      | awk '{print $2" "$3" "$4}' )
    LAST=$(( START + FRAMECOUNT - 1 ))
    if [ ${CONDID} == 0 ] ; then
      if [ ${SEQID} == 9 ] ; then
        CMD=" $MMETRIC \
          sequence  \
            --firstFrame    ${START} \
            --lastFrame     ${LAST} \
            END \
          compare \
            --mode          equ \
            --inputModelA   ${SRCMESH} \
            --inputModelB   ${NAME}_%04d_dec_raw.obj \
          END \
          > ${LOGMET}"
      else
        CMD=" $MMETRIC \
          sequence  \
            --firstFrame    ${START} \
            --lastFrame     ${LAST} \
            END \
          compare \
            --mode          equ \
            --inputModelA   ${SRCMESH} \
            --inputModelB   ${NAME}_%04d_dec_raw.obj \
            --inputMapB     ${NAME}_%04d_dec.png \
          END \
          > ${LOGMET}"
		fi
    else
      GRIDSIZE=$(    cat ${CFGSUBDIR}/mmetric.cfg | grep "gridSize:"         | awk '{print $2}' )
      RESOLUTION=$(  cat ${CFGSUBDIR}/mmetric.cfg | grep "resolution:"       | awk '{print $2}' )
      CMD="$MMETRIC \
        sequence  \
          --firstFrame    ${START} \
          --lastFrame     ${LAST} \
          END \
        dequantize \
          --inputModel    ${SRCMESH} \
          --useFixedPoint \
          --qp            ${QP} \
          --qt            ${QT} \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --minUv         \"0.0 0.0\" \
          --maxUv         \"1.0 1.0\" \
          --outputModel   ID:deqRef \
          END \
        dequantize \
          --inputModel    ${NAME}_%04d_dec_raw.ply \
          --useFixedPoint \
          --qp            ${QP} \
          --qt            ${QT} \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --minUv         \"0.0 0.0\" \
          --maxUv         \"1.0 1.0\" \
          --outputModel   ID:deqDis \
          END \
        compare \
          --mode          ibsm \
          --inputModelA   ID:deqRef \
          --inputModelB   ID:deqDis \
          --inputMapB     ${NAME}_%04d_dec.png \
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
        sample  \
          --inputModel    ID:ref_reordered \
          --mode          grid \
          --useNormal     \
          --useFixedPoint \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --bilinear      \
          --gridSize      ${GRIDSIZE} \
          --hideProgress  1 \
          --outputModel   ID:ref_pc \
        END \
        sample  \
          --inputModel    ID:dis_reordered \
          --inputMap      ${NAME}_%04d_dec.png \
          --mode          grid \
          --useNormal     \
          --useFixedPoint \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --bilinear      \
          --gridSize      ${GRIDSIZE} \
          --hideProgress  1 \
          --outputModel   ID:dis_pc \
        END \
        compare \
          --mode          pcc  \
          --inputModelA   ID:ref_pc \
          --inputModelB   ID:dis_pc \
          --resolution    ${RESOLUTION} \
        > ${LOGMET}"
    fi
    if (( $VERBOSE )) ; then
      echo -e "\033[0;32mMMetric: ${NAME} \033[0m";
      formatCmd "$CMD" "-- -c > compare dequantize sequence reindex sample END"
    fi
    if ! eval $CMD ; then echo "ERROR: mmetric sw return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${LOGMET} already exist"; fi
  fi
}

function mmetric_ORIGINAL() {
  if isNotFinishMM ${LOGMET} ;
  then
    START=$(       cat ${CFGSUBDIR}/mmetric.cfg | grep "startFrameIndex:"  | awk '{print $2}' )
    SRCMESH=$(     cat ${CFGSUBDIR}/mmetric.cfg | grep "srcMesh:"          | awk '{print $2}' )
    SRCTEX=$(      cat ${CFGSUBDIR}/mmetric.cfg | grep "srcTex:"           | awk '{print $2}' )
    QP=$(          cat ${CFGSUBDIR}/mmetric.cfg | grep "positionBitDepth:" | awk '{print $2}' )
    QT=$(          cat ${CFGSUBDIR}/mmetric.cfg | grep "texCoordBitDepth:" | awk '{print $2}' )
    MINPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "minPosition:"      | awk '{print $2" "$3" "$4}' )
    MAXPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "maxPosition:"      | awk '{print $2" "$3" "$4}' )
    LAST=$(( START + FRAMECOUNT - 1 ))
    if [ ${CONDID} == 0 ] ; then
      CMD=" $MMETRIC \
        sequence  \
          --firstFrame    ${START} \
          --lastFrame     ${LAST} \
          END \
        compare \
          --mode          equ \
          --inputModelA   ${SRCMESH} \
          --inputModelB   ${NAME}_%04d_dec_raw.obj \
          --inputMapA     ${SRCTEX} \
          --inputMapB     ${NAME}_%04d_dec.png \
        END \
        > ${LOGMET}"
    else
      GRIDSIZE=$(    cat ${CFGSUBDIR}/mmetric.cfg | grep "gridSize:"         | awk '{print $2}' )
      RESOLUTION=$(  cat ${CFGSUBDIR}/mmetric.cfg | grep "resolution:"       | awk '{print $2}' )
      CMD="$MMETRIC \
        sequence  \
          --firstFrame    ${START} \
          --lastFrame     ${LAST} \
          END \
        dequantize \
          --inputModel    ${SRCMESH} \
          --useFixedPoint \
          --qp            ${QP} \
          --qt            ${QT} \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --minUv         \"0.0 0.0\" \
          --maxUv         \"1.0 1.0\" \
          --outputModel   ID:deqRef \
          END \
        dequantize \
          --inputModel    ${NAME}_%04d_dec_raw.ply \
          --useFixedPoint \
          --qp            ${QP} \
          --qt            ${QT} \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --minUv         \"0.0 0.0\" \
          --maxUv         \"1.0 1.0\" \
          --outputModel   ID:deqDis \
          END \
        compare \
          --mode          ibsm \
          --inputModelA   ID:deqRef \
          --inputModelB   ID:deqDis \
          --inputMapA     ${SRCTEX} \
          --inputMapB     ${NAME}_%04d_dec.png \
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
        sample  \
          --inputModel    ID:ref_reordered \
          --inputMap      ${SRCTEX} \
          --mode          grid \
          --useNormal     \
          --useFixedPoint \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --bilinear      \
          --gridSize      ${GRIDSIZE} \
          --hideProgress  1 \
          --outputModel   ID:ref_pc \
        END \
        sample  \
          --inputModel    ID:dis_reordered \
          --inputMap      ${NAME}_%04d_dec.png \
          --mode          grid \
          --useNormal     \
          --useFixedPoint \
          --minPos        \"${MINPOSITION}\" \
          --maxPos        \"${MAXPOSITION}\" \
          --bilinear      \
          --gridSize      ${GRIDSIZE} \
          --hideProgress  1 \
          --outputModel   ID:dis_pc \
        END \
        compare \
          --mode          pcc  \
          --inputModelA   ID:ref_pc \
          --inputModelB   ID:dis_pc \
          --resolution    ${RESOLUTION} \
        > ${LOGMET}"
    fi
    if (( $VERBOSE )) ; then
      echo -e "\033[0;32mMMetric: ${NAME} \033[0m";
      formatCmd "$CMD" "-- -c > compare dequantize sequence reindex sample END"
    fi
    if ! eval $CMD ; then echo "ERROR: mmetric sw return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${LOGMET} already exist"; fi
  fi
}

# Render frame excerpt process using mmetric renderer
function render_multiple_textures() {
  if [ ! -f ${OUTDIR}/${1}_light1.png ] && [ ! -f ${OUTDIR}/${1}_light0.png ] ; then
    RENDERWIDTH=960
    RENDERHEIGHT=1920
    INDEXFRAME=4
    START=$(   cat ${CFGSUBDIR}/mmetric.cfg | grep "startFrameIndex:"  | awk '{print $2}' )
    SRCMESH=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "srcMesh:"          | awk '{print $2}' )
    QP=$(      cat ${CFGSUBDIR}/mmetric.cfg | grep "positionBitDepth:" | awk '{print $2}' )
    QT=$(      cat ${CFGSUBDIR}/mmetric.cfg | grep "texCoordBitDepth:" | awk '{print $2}' )
    MINPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "minPosition:" | awk '{print $2" "$3" "$4}' )
    MAXPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "maxPosition:" | awk '{print $2" "$3" "$4}' )
    LAST=$(( START + FRAMECOUNT - 1 ))
    INDEXFRAME=$(( $START + $INDEXFRAME ))
    INDEXFRAME=$(( $INDEXFRAME < $LAST ? $INDEXFRAME : $LAST ))
    CMD="${MMETRIC} \
      sequence \
        --firstFrame    ${INDEXFRAME} \
        --lastFrame     ${INDEXFRAME} \
      END \
      dequantize \
        --inputModel    ${SRCMESH} \
        --outputModel   ID:deqRef \
        --useFixedPoint \
        --qp            ${QP} \
        --qt            ${QT} \
        --minPos        \"${MINPOSITION}\" \
        --maxPos        \"${MAXPOSITION}\" \
        --minUv         \"0.0 0.0\" \
        --maxUv         \"1.0 1.0\" \
        END \
      render \
        --inputModel    ID:deqRef\
        --enableCulling \
        --bboxMin       \"${MINPOSITION}\" \
        --bboxMax       \"${MAXPOSITION}\" \
        --clearColor    \"128 128 128 0\" \
        --viewDir       \"${VIEW}\" \
        --width         ${RENDERWIDTH} \
        --height        ${RENDERHEIGHT} \
        --outputImage   ${OUTDIR}/${1}_light0.png  \
      END \
      render \
        --inputModel    ID:deqRef\
        --enableCulling \
        --enableLighting \
        --bboxMin       \"${MINPOSITION}\" \
        --bboxMax       \"${MAXPOSITION}\" \
        --clearColor    \"128 128 128 0\" \
        --viewDir       \"${VIEW}\" \
        --autoLightPosition \
        --lightAutoDir  \"${LIGHT}\" \
        --width         ${RENDERWIDTH} \
        --height        ${RENDERHEIGHT} \
        --outputImage   ${OUTDIR}/${1}_light1.png  \
        > ${OUTDIR}/render_${1}.log"
    if (( $VERBOSE )) ; then formatCmd "$CMD" "-- > sequence dequantize render END "; fi
    if ! eval $CMD ; then echo "ERROR: metrics sw return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${OUTDIR}/${1}_light0.png and ${OUTDIR}/${1}_light1.png already exist"; fi
  fi
}

function render() {
  if [ ! -f ${OUTDIR}/${1}_light1.png ] && [ ! -f ${OUTDIR}/${1}_light0.png ] ; then
    RENDERWIDTH=960
    RENDERHEIGHT=1920
    INDEXFRAME=4
    START=$(   cat ${CFGSUBDIR}/mmetric.cfg | grep "startFrameIndex:"  | awk '{print $2}' )
    SRCMESH=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "srcMesh:"          | awk '{print $2}' )
    SRCTEX=$(  cat ${CFGSUBDIR}/mmetric.cfg | grep "srcTex:"           | awk '{print $2}' )
    QP=$(      cat ${CFGSUBDIR}/mmetric.cfg | grep "positionBitDepth:" | awk '{print $2}' )
    QT=$(      cat ${CFGSUBDIR}/mmetric.cfg | grep "texCoordBitDepth:" | awk '{print $2}' )
    MINPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "minPosition:" | awk '{print $2" "$3" "$4}' )
    MAXPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "maxPosition:" | awk '{print $2" "$3" "$4}' )
    LAST=$(( START + FRAMECOUNT - 1 ))
    INDEXFRAME=$(( $START + $INDEXFRAME ))
    INDEXFRAME=$(( $INDEXFRAME < $LAST ? $INDEXFRAME : $LAST ))
    if [ $# == 3 ] ; then MESH=$2; TEX=$3; else MESH=$SRCMESH; TEX=$SRCTEX; fi
    CMD="${MMETRIC} \
      sequence \
        --firstFrame    ${INDEXFRAME} \
        --lastFrame     ${INDEXFRAME} \
      END \
      dequantize \
        --inputModel    ${MESH} \
        --outputModel   ID:deqRef \
        --useFixedPoint \
        --qp            ${QP} \
        --qt            ${QT} \
        --minPos        \"${MINPOSITION}\" \
        --maxPos        \"${MAXPOSITION}\" \
        --minUv         \"0.0 0.0\" \
        --maxUv         \"1.0 1.0\" \
        END \
      render \
        --inputModel    ID:deqRef\
        --inputMap      ${TEX} \
        --enableCulling \
        --bboxMin       \"${MINPOSITION}\" \
        --bboxMax       \"${MAXPOSITION}\" \
        --clearColor    \"128 128 128 0\" \
        --viewDir       \"${VIEW}\" \
        --width         ${RENDERWIDTH} \
        --height        ${RENDERHEIGHT} \
        --outputImage   ${OUTDIR}/${1}_light0.png  \
      END \
      render \
        --inputModel    ID:deqRef\
        --inputMap      ${TEX} \
        --enableCulling \
        --enableLighting \
        --bboxMin       \"${MINPOSITION}\" \
        --bboxMax       \"${MAXPOSITION}\" \
        --clearColor    \"128 128 128 0\" \
        --viewDir       \"${VIEW}\" \
        --autoLightPosition \
        --lightAutoDir  \"${LIGHT}\" \
        --width         ${RENDERWIDTH} \
        --height        ${RENDERHEIGHT} \
        --outputImage   ${OUTDIR}/${1}_light1.png  \
        > ${OUTDIR}/render_${1}.log"
    if (( $VERBOSE )) ; then formatCmd "$CMD" "-- > sequence dequantize render END "; fi
    if ! eval $CMD ; then echo "ERROR: metrics sw return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${OUTDIR}/${1}_light0.png and ${OUTDIR}/${1}_light1.png already exist"; fi
  fi
}

function seq_dequantize() {
  LOGDEQ=${OUTDIR}/seq_dequantize.log
  if [ ! -f ${LOGDEQ} ] ; 
  then
    START=$(       cat ${CFGSUBDIR}/mmetric.cfg | grep "startFrameIndex:"  | awk '{print $2}' )
    SRCMESH=$(     cat ${CFGSUBDIR}/mmetric.cfg | grep "srcMesh:"          | awk '{print $2}' )
    SRCTEX=$(      cat ${CFGSUBDIR}/mmetric.cfg | grep "srcTex:"           | awk '{print $2}' )
    QP=$(          cat ${CFGSUBDIR}/mmetric.cfg | grep "positionBitDepth:" | awk '{print $2}' )
    QT=$(          cat ${CFGSUBDIR}/mmetric.cfg | grep "texCoordBitDepth:" | awk '{print $2}' )
    MINPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "minPosition:"      | awk '{print $2" "$3" "$4}' )
    MAXPOSITION=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "maxPosition:"      | awk '{print $2" "$3" "$4}' )
    LAST=$(( START + FRAMECOUNT - 1 ))

	#	
	CMD="${MMETRIC} \
      sequence  \
        --firstFrame    ${START} \
        --lastFrame     ${LAST} \
      END \
      dequantize \
        --inputModel    ${1} \
        --outputModel   ${2} \
        --useFixedPoint \
        --qp            ${QP} \
        --qt            ${QT} \
        --minPos        \"${MINPOSITION}\" \
        --maxPos        \"${MAXPOSITION}\" \
        --minUv         \"0.0 0.0\" \
        --maxUv         \"1.0 1.0\" \
	    > ${OUTDIR}/seq_dequantize.log"
		
	if (( $VERBOSE )) ; then formatCmd "$CMD" "-- > "; fi
    if ! eval $CMD ; then echo "ERROR: sequence dequantize return !0"; exit; fi
  else
    if (( $VERBOSE )) ; then echo "${LOGDEQ} already exist"; fi
  fi
}

# Render path process using MPEG renderer
function render_video() {
  if [ ! -f ${VIDEODIR}/${1}_x265lossless.mp4 ] ; then
    RENDERWIDTH=1080
    RENDERHEIGHT=1920
    START=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "startFrameIndex:"  | awk '{print $2}' )
	if [ $SEQID == 9 ] ; then FLOOR=0; else FLOOR=1; fi
		
	# render the path
    CMD="$RENDERER \
        --frameNumber=${FRAMECOUNT} \
        --frameIndex=${START} \
        --fps=30 \
        --width=1920 \
        --height=1080 \
        --rotate=0 \
        --playBackward=1 \
        --play=1 \
        --overlay=0 \
        --size=1 \
        --type=0 \
        --binary=0 \
        --visible=0 \
        --spline=0 \
        --floor=${FLOOR} \
        --lighting=0 \
        --softwareRenderer=1 \
        --camera=${CAMPATH} \
        --PlyFile=${2} \
        --RgbFile=${VIDEODIR}/${1} \
        > ${OUTDIR}/render_video_mpeg_renderer.log"
    if (( $VERBOSE )) ; then formatCmd "$CMD" "-- > "; fi
    if ! eval $CMD ; then echo "ERROR: mpeg renderer sw return !0"; exit; fi

    #now convert rgb file to 265 video file
    CMD="$FFMPEG \
        -y \
        -f rawvideo \
        -r 30 \
        -pix_fmt rgb48 \
        -s 1920x1080 \
        -threads 4 \
        -i ${VIDEODIR}/${1}*.rgb \
        -c:v libx265 \
        -preset fast \
        -x265-params lossless=1 \
        -pix_fmt yuv420p10le \
        ${VIDEODIR}/${1}_x265lossless.mp4 \
        > ${OUTDIR}/render_video_ffmpeg.log";
    if (( $VERBOSE )) ; then formatCmd "$CMD" "- > "; fi
    if ! eval $CMD ; then  echo "ERROR: ffmpeg sw return !0"; exit;  fi
    if (( $VERBOSE )) ; then echo rm ${VIDEODIR}/${1}*.rgb; fi
    rm ${VIDEODIR}/${1}*.rgb
  else
    if (( $VERBOSE )) ; then echo "${VIDEODIR}/${1}_x265lossless.mp4 already exist"; fi
  fi
}

function render_video_SAVE() {
  if [ ! -f ${VIDEODIR}/${1}_x265lossless.mp4 ] ; then
    RENDERWIDTH=1080
    RENDERHEIGHT=1920
    START=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "startFrameIndex:"  | awk '{print $2}' )
		
	# render the path
    CMD="$RENDERER \
        --frameNumber=${FRAMECOUNT} \
        --frameIndex=${START} \
        --fps=30 \
        --width=1920 \
        --height=1080 \
        --rotate=0 \
        --playBackward=1 \
        --play=1 \
        --overlay=0 \
        --size=1 \
        --type=0 \
        --binary=0 \
        --visible=0 \
        --spline=0 \
        --floor=1 \
        --lighting=0 \
        --softwareRenderer=1 \
        --camera=${CAMPATH} \
        --PlyFile=${2} \
        --RgbFile=${VIDEODIR}/${1} \
        > ${OUTDIR}/render_video_mpeg_renderer.log"
    if (( $VERBOSE )) ; then formatCmd "$CMD" "-- > "; fi
    if ! eval $CMD ; then echo "ERROR: mpeg renderer sw return !0"; exit; fi

    #now convert rgb file to 265 video file
    CMD="$FFMPEG \
        -y \
        -f rawvideo \
        -r 30 \
        -pix_fmt rgb48 \
        -s 1920x1080 \
        -threads 4 \
        -i ${VIDEODIR}/${1}*.rgb \
        -c:v libx265 \
        -preset fast \
        -x265-params lossless=1 \
        -pix_fmt yuv420p10le \
        ${VIDEODIR}/${1}_x265lossless.mp4 \
        > ${OUTDIR}/render_video_ffmpeg.log";
    if (( $VERBOSE )) ; then formatCmd "$CMD" "- > "; fi
    if ! eval $CMD ; then  echo "ERROR: ffmpeg sw return !0"; exit;  fi
    if (( $VERBOSE )) ; then echo rm ${VIDEODIR}/${1}*.rgb; fi
    rm ${VIDEODIR}/${1}*.rgb
  else
    if (( $VERBOSE )) ; then echo "${VIDEODIR}/${1}_x265lossless.mp4 already exist"; fi
  fi
}

function collect() {
  PARAMS=()
  # if [ $VERBOSE == 0  ] ; then PARAMS+=( "--quiet" ); fi
  if [ "$CSV"   != "" ] ; then PARAMS+=( "--csv $CSV" ); fi
  CMD="${CURDIR}/collect_results.sh \
    --condId  ${CONDID} \
    --seqId   ${SEQID}  \
    --rateId  ${RATEID} \
    --vdmc    ${VDMC}   \
    --logenc  ${LOGENC} \
    --logdec  ${LOGDEC} \
    --logmet  ${LOGMET}\
    ${PARAMS[@]}"
  if (( $VERBOSE )) ; then
    echo -e "\033[0;32mCollect: ${NAME} \033[0m";
    formatCmd "$CMD" "--"
  fi
  if ! eval $CMD ; then echo "ERROR: collect return !0"; exit; fi
}

function print_usage() {
  echo "$0 execute encoding/decoding/metrics "
  echo "";
  echo "  Usage:"
  echo "    -h|--help   : print help ";
  echo "    -q|--quiet  : disable logs            (default: $VERBOSE )";
  echo "    -f|--frames : frame count             (default: $FRAMECOUNT )";
  echo "    -c|--cfgdir : configured directory    (default: \"$CFGDIR\" )";
  echo "    -o|--outdir : output directory        (default: \"$OUTDIR\" )";
  echo "    --condId=   : condition: 0,1,2        (default: $CONDID )";
  echo "    --seqId=    : seq: 1,2,3,4,5,6,7,8    (default: $SEQID )";
  echo "    --rateId=   : Rate: 1,2,3,4,5         (default: $RATEID )";
  echo "    --tmmMetric : Use TMM metric software (default: $TMMMETRIC )";
  echo "    --render    : Create rendered images  (default: $RENDER )";
  echo "    --video     : Create rendered video   (default: $VIDEO )";
  echo "    --encParams : configured directory    (default: \"${ENCPARAMS[@]}\" )";
  echo "    --decParams : configured directory    (default: \"${DECPARAMS[@]}\" )";
  echo "    --csv       : generate .csv file      (default: \"$CSV\" )";
  echo "    --ffmpeg=   : path to ffmpeg sw       (default: \"$FFMPEG\" )";
  echo "";
  echo "  Examples:";
  echo "    $0 -h";
  echo "    $0 --condId=1 --seqId=3 --rateId=3 --cfgdir=generatedConfigFiles ";
  echo "    $0 --condId=1 --seqId=3 --rateId=3 --cfgdir=generatedConfigFiles --TMMMETRIC ";
  echo "    ";
  if [ "$#" != 0 ] ; then echo -e "ERROR: $1 \n"; fi
  exit 0;
}

# Set default parameters
FRAMECOUNT=1
VERBOSE=1
OUTDIR="results"
CFGDIR=""
CONDID=1
SEQID=1
RATEID=1
TMMMETRIC=0
RENDER=0;
DEQUANT=0
VIDEO=0
ENCPARAMS=()
DECPARAMS=()
CSV=
FFMPEG=

# Parse input parameters
while [[ $# -gt 0 ]] ; do
  C=$1; if [[ "$C" =~ [=] ]] ; then V=${C#*=}; elif [[ $2 == -* ]] ; then  V=""; else V=$2; shift; fi;
  case "$C" in
    -h|--help    ) print_usage;;
    --quiet      ) VERBOSE=0;;
    -f|--frame   ) FRAMECOUNT=$V;;
    -o|--outdir  ) OUTDIR=$V;;
    -c|--cfgdir  ) CFGDIR=$V;;
    --condId     ) CONDID=$V;;
    --seqId      ) SEQID=$V;;
    --rateId     ) RATEID=$V;;
    --tmmMetric  ) TMMMETRIC=1;;
    --render     ) RENDER=1;;
	--dequantize ) DEQUANT=1;;
    --video      ) VIDEO=1;;
    --encParams* ) ENCPARAMS+=( $V );;
    --decParams* ) DECPARAMS+=( $V );;
    --csv        ) CSV=$V;;
    --ffmpeg     ) FFMPEG=$V;;
    *            ) print_usage "unsupported arguments: $C ";;
  esac
  shift;
done

# Set encoder/decoder/metric sw path
setSoftwarePath;

# Check parameters
case $CONDID in
  0) RATEID=0;;
  1) ;;
  2) ;;
  *) print_usage "condId not valid ($CONDID)";;
esac
case $SEQID in
  1) VIEW="0.0 0.0  1.0"; LIGHT=" 1.0 1.0  1.0"; SEQ="longdress";;
  2) VIEW="0.0 0.0  1.0"; LIGHT=" 1.0 1.0  1.0"; SEQ="soldier";;
  3) VIEW="0.0 0.0 -1.0"; LIGHT="-1.0 1.0 -1.0"; SEQ="basketball_player";;
  4) VIEW="0.0 0.0 -1.0"; LIGHT="-1.0 1.0 -1.0"; SEQ="dancer";;
  5) VIEW="0.0 0.0  1.0"; LIGHT=" 1.0 1.0  1.0"; SEQ="mitch";;
  6) VIEW="0.0 0.0  1.0"; LIGHT=" 1.0 1.0  1.0"; SEQ="thomas";;
  7) VIEW="0.0 0.0 -1.0"; LIGHT="-1.0 1.0 -1.0"; SEQ="football";;
  8) VIEW="0.0 0.0  1.0"; LIGHT=" 1.0 1.0  1.0"; SEQ="levi";;
  9) VIEW="0.0 -1.0 0.0"; LIGHT=" 0.0 -1.0  0.0"; SEQ="rwtt422";;
  *) print_usage "seqId not valid ($SEQID)";;
esac
case $RATEID in
  0) RATE="";;
  1) RATE="r1";;
  2) RATE="r2";;
  3) RATE="r3";;
  4) RATE="r4";;
  5) RATE="r5";;
  *) print_usage "rateId not valid ($RATEID)";;
esac
if [ -z "${CAMPATH}" ]; then
  case $SEQID in
    1) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_longdress_dq1.txt;;
    2) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_longdress_dq1.txt;;
    3) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_basketball_player_dq1.txt;;
    4) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_basketball_player_dq1.txt;;
    5) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_mitch_dq1.txt;;
    6) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_mitch_dq1.txt;;
    7) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_football_dq2-spline0.txt;;
    8) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_levi_dq2.txt;;
    9) CAMPATH=${MAINDIR}/cfg/renderer/camerapath_rwtt_dq1.txt;;
    *) print_usage "seqId not valid ($SEQID)";;
  esac
fi

# Check configuration files
CFGSUBDIR=$( cd "$CFGDIR" && pwd )/s${SEQID}c${CONDID}${RATE}_${SEQ:0:4}/
if [ ! -d $CFGDIR ] || [ ! -f ${CFGSUBDIR}/encoder.cfg ] || [ ! -f ${CFGSUBDIR}/decoder.cfg ] || [ ! -f ${CFGSUBDIR}/mmetric.cfg ]
then
  print_usage "Config directory is not valid (${CFGSUBDIR}) \n please run ./scripts/create_configuration_files.sh";
fi

# Set output directory
NAME=$( basename "$CFGSUBDIR" )
if [ ${VIDEO} == 1 ] ; then
  VIDEODIR=${OUTDIR}/$( printf "F%03d" "$FRAMECOUNT" )/video;
  if [ ! -d $VIDEODIR ] ; then mkdir -p $VIDEODIR; fi
fi
OUTDIR=${OUTDIR}/$( printf "F%03d" "$FRAMECOUNT" )/${NAME};
if [ ! -d $OUTDIR ] ; then mkdir -p $OUTDIR; fi
NAME=${OUTDIR}/${NAME}
VDMC=${NAME}.vmesh
FCOUNTMAX=$( cat ${CFGSUBDIR}/mmetric.cfg | grep "frameCount:" | awk '{print $2}' )
FRAMECOUNT=$( clip $FRAMECOUNT 1 $FCOUNTMAX )
LOGENC=${OUTDIR}/encoder.log
LOGDEC=${OUTDIR}/decoder.log
LOGMET=${OUTDIR}/metrics.log

# Start sub-processes
encode
decode
if [ ${TMMMETRIC} == 1 ] ; then metrics; else mmetric; fi
# render still images using mmetric (dequantize on the flight)
if [ ${RENDER} == 1 ] ; then
  if [ ${CONDID} == 0 ] ; then
  render dec ${NAME}_%04d_dec_raw.obj ${NAME}_%04d_dec.png
  else
  render dec ${NAME}_%04d_dec_raw.ply ${NAME}_%04d_dec.png
  fi
  render_multiple_textures src
fi
# video rendering requires dequantized sequence
if [[ ${DEQUANT} == 1 || ${VIDEO} == 1 ]] ; then
  if [ ${CONDID} == 0 ] ; then
  seq_dequantize ${NAME}_%04d_dec_raw.obj ${NAME}_%04d_dec.obj
  else
  seq_dequantize ${NAME}_%04d_dec_raw.ply ${NAME}_%04d_dec.obj
  fi
fi
if [ ${VIDEO} == 1 ] ; then
  if [ ${CONDID} == 0 ] ; then
  render_video $( basename "$CFGSUBDIR" ) ${NAME}_%04d_dec.obj 
  else
  render_video $( basename "$CFGSUBDIR" ) ${NAME}_%04d_dec.obj
  fi
fi

# Get metrics results
collect
