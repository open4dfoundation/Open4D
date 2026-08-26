#!/bin/bash

CURDIR=$( cd "$( dirname "$0" )" && pwd );

# Experiences
CONDS="0 1 2";
SEQS="9 1 2 3 4 7 8 5 6";
#SEQS="9";
RATES=( "0" "1 2 3 4 5" "1 2 3 4 5" );
SEQNAME=( "long" "sold" "bask" "danc" "mitc" "thom" "foot" "levi" "rwtt")

function formatCmd(){
  local f=${1};
  for s in ${2} ; do
    if [ $s == "--" ] ; then f=${f//${s}/ \\\\\\n    ${s}};  else f=${f// ${s}/ \\\\\\n   ${s}};  fi
  done
  echo -e "$f" | sed 's/ *\\$/ \\/'
}

function trim() {
  local str="$*"
  str="${str#"${str%%[![:space:]]*}"}"
  str="${str%"${str##*[![:space:]]}"}"
  echo $str
}

function getCsvFilename(){ echo ${OUTDIR}/${1}/$( printf "F%03d" $FRAMECOUNT )_${1}.csv; }

function getRunning() { echo $( ps | grep run.sh | wc -l ); }

function readCsv(){
  if [ ! -f $1 ] ; then echo "$1 not exists "; exit -1; fi
  while IFS="," read -r arg0 arg1 arg2 ;  do
    EXPERIMENTS[$NUMTESTS,name]=$( trim $arg0 )
    EXPERIMENTS[$NUMTESTS,encParams]=$( trim $arg1 )
    EXPERIMENTS[$NUMTESTS,decParams]=$( trim $arg2 )
    NUMTESTS=$((NUMTESTS +1 ))
  done < <(tail -n +2 $1)
}

function getRunCmd(){
  ENCPARAMS=(); for p in ${EXPERIMENTS[$i,encParams]} ; do ENCPARAMS+=( "--encParams \\\"$p\\\"" ); done
  DECPARAMS=(); for p in ${EXPERIMENTS[$i,decParams]} ; do DECPARAMS+=( "--decParams \\\"$p\\\"" ); done
  echo "${CURDIR}/run.sh \
          --frame     $FRAMECOUNT \
          --outdir    ${OUTDIR}/${EXPERIMENTS[$i,name]} \
          --cfgdir    $CFGDIR \
          --condId    $CONDID \
          --seqId     $SEQID \
          --rateId    $RATEID \
          ${ENCPARAMS[@]} \
          ${DECPARAMS[@]} \
          ${PARAMS[@]}"
}

function print_usage() {
  echo "$0 execute all encoding/decoding/metrics "
  echo "";
  echo "  Usage:"
  echo "    -h|--help      : print help ";
  echo "    -q|--quiet     : disable logs                    (default: $VERBOSE )"
  echo "    -f|--frames    : frame count                     (default: $FRAMECOUNT )"
  echo "    -c|--cfgdir    : configured directory            (default: \"$CFGDIR\" )"
  echo "    -o|--outdir    : output directory                (default: $OUTDIR )"
  echo "    -r|--refresh   : remove .csv result and re-run   (default: $REFRESH )"
  echo "    --experiments  : csv configuration files         (default: $CSVFILE )"
  echo "    --tmmMetric    : Use TMM metric software         (default: 0 )"
  echo "    -t|--threads   : Number of parallel experiments  (default: $THREADS )"
  echo "    --render       : Create pdf  rendered images     (default: $RENDER )"
  echo "    --dequantize   : Dequantize decompressed models  (default: $DEQUANT )"
  echo "    --graph        : Create pdf with metric graphs   (default: $GRAPH )"
  echo "    --xlsm         : Create CTC xlsm files           (default: $XLSM )"
  echo "    --video        : Create video                    (default: $VIDEO )"
  echo "    --ffmpeg       : path to ffmpeg sw               (default: $FFMPEG )"
  echo ""
  echo "  Examples:"
  echo "    $0 -h"
  echo "    $0 \\"
  echo "      --experiments ./scripts/test.csv \\"
  echo "      --outdir      experiments \\"
  echo "      --cfgdir      generatedConfigFilesHM \\"
  echo "      --frame       2 \\"
  echo "      --graph \\"
  echo "      --render \\"
  echo "      --xlsm \\"
  echo "      --quiet";
  echo "";
  if [ "$#" != 0 ] ; then echo -e "ERROR: $1 \n"; fi
  exit 0;
}

# Set default parameters
FRAMECOUNT=1
PARAMS=()
VERBOSE=1
OUTDIR="experiments"
CFGDIR="generatedConfigFiles/hm"
TMMMETRIC=
RENDER=0
DEQUANT=0
GRAPH=0
XLSM=0
CSVFILE=${CURDIR}/test.csv
THREADS=1
REFRESH=0
VIDEO=0
FFMPEG=ffmpeg

# Parse input parameters
while [[ $# -gt 0 ]] ; do
  C=$1; if [[ "$C" =~ [=] ]] ; then V=${C#*=}; elif [[ $2 == -* ]] ; then  V=""; else V=$2; shift; fi;
  case "$C" in
    -h|--help    ) print_usage;;
    -q|--quiet   ) VERBOSE=0; PARAMS+=( --quiet );;
    -f|--frame*  ) FRAMECOUNT=$V;;
    -o|--outdir* ) OUTDIR=$V;;
    -c|--cfgdir* ) CFGDIR=$V;;
    -r|--refresh ) REFRESH=1;;
    --experiments) CSVFILE=$V;;
    -t|--threads ) THREADS=$V;;
    --tmmMetric  ) PARAMS+=( --tmmMetric );;
    --render     ) RENDER=1; PARAMS+=( --render );;
	--dequantize ) DEQUANT=1; PARAMS+=( --dequantize );;
    --video      ) VIDEO=1;;
    --graph      ) GRAPH=1;;
    --xlsm       ) XLSM=1;;
    --ffmpeg     ) FFMPEG=$V;;
    *            ) print_usage "unsupported arguments: $C ";;
  esac
  shift;
done
if [ $VIDEO == 1 ] ; then PARAMS+=( --video --ffmpeg ${FFMPEG} ); fi

function isNotFinish() {
  if [ ! -f $CSV ] ; then true; return; fi
  RESULTS=( $( cat $CSV | grep "$SEQID,$CONDID,$RATEID," | \
             awk -F ',' '{ printf("%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s ", \
               $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16 ) }' ) )
  for ((j=0;j<16;j++)) ; do if [ "${RESULTS[$j]}" == "" ] ; then true; return; fi; done
  if (( ${RENDER} )) ; then
    if [ $RATEID == 0 ]; then RATE=""; else RATE=r${RATEID}; fi
    DIR=${OUTDIR}/${EXPERIMENTS[$i,name]}/$( printf "F%03d" "$FRAMECOUNT" )/s${SEQID}c${CONDID}${RATE}_${SEQNAME[$((SEQID-1))]}
    if [ ! -f ${DIR}/src_light1.png ] || [ ! -f ${DIR}/src_light0.png ] || \
       [ ! -f ${DIR}/dec_light1.png ] || [ ! -f ${DIR}/dec_light0.png ] ; then
      echo "render not done "
      true;
      return;
    fi
  fi
  if (( ${VIDEO} )) ; then
    if [ $RATEID == 0 ]; then RATE=""; else RATE=r${RATEID}; fi
    DIR=${OUTDIR}/${EXPERIMENTS[$i,name]}/$( printf "F%03d" "$FRAMECOUNT" )/video
    if [ ! -f ${DIR}/s${SEQID}c${CONDID}${RATE}_${SEQNAME[$((SEQID-1))]}_x265lossless.mp4 ] ; then
      echo "video not done "
      true;
      return;
    fi
  fi
  false
}

function isNotFinishAll() {
  if [ $REFRESH == 1 ] ; then true; return; fi
  if [ ! -f $CSV ] ; then true; return; fi
  for CONDID in $CONDS ; do
    for SEQID in $SEQS; do
      for RATEID in ${RATES[((${CONDID}))]}; do
        if isNotFinish ; then true; return; fi
      done
    done
  done
  false
}

# Parse experiments configurations
declare -A EXPERIMENTS
NUMTESTS=0;
readCsv $CSVFILE
for ((i=0;i<NUMTESTS;i++)) ; do
  printf "%d: %-20s Enc: %-75s Dec = %-75s \n" $i "${EXPERIMENTS[$i,name]}" "${EXPERIMENTS[$i,encParams]}" "${EXPERIMENTS[$i,decParams]}"
done

# Start multi-threaded experiments
if [ $THREADS -gt 1 ] ; then
  for ((i=0;i<NUMTESTS;i++)) ; do
    CSV=$( getCsvFilename ${EXPERIMENTS[$i,name]} )
    if isNotFinishAll ; then
      for CONDID in ${CONDS}; do
        for SEQID in ${SEQS}; do
          for RATEID in ${RATES[((${CONDID}))]}; do
            if isNotFinish ; then
              # Start encode/decode/metric/renderer without create csv files
              echo "start $CONDID $SEQID $RATEID ${EXPERIMENTS[$i,name]} Thread = $( getRunning ) / ${THREADS}"
              CMD="$( getRunCmd ) &"
              if (( $VERBOSE )) ; then formatCmd "$CMD" "-- "; fi
              while [ $( getRunning ) -ge $THREADS ] ; do sleep 1; done;
              if ! eval $CMD ; then echo "ERROR: gnuplot graphs return !0"; fi
            fi
          done
        done
      done
    fi
  done
  echo "All experiments have begun, wait for them to end"
  while [ $( getRunning ) -ge 1 ] ; do sleep 1; done
fi

# Start experiments and csv creation
for ((i=0;i<NUMTESTS;i++)) ; do
  echo "${EXPERIMENTS[$i,name]} EncParams = ${EXPERIMENTS[$i,encParams]} DecParams = ${EXPERIMENTS[$i,decParams]}"
  CSV=$( getCsvFilename ${EXPERIMENTS[$i,name]} )
  if isNotFinishAll ; then
    rm -f ${CSV}
    for CONDID in ${CONDS}; do
      for SEQID in ${SEQS}; do
        for RATEID in ${RATES[((${CONDID}))]}; do
          CMD="$( getRunCmd ) --csv $CSV "
          if (( $VERBOSE )) ; then formatCmd "$CMD" "-- "; fi
          if ! eval $CMD ; then echo "ERROR: gnuplot graphs return !0"; fi
        done
      done
    done
  else
    echo "${CSV} already exists and all experiments are finish."
  fi
done

# Create graph and render pdf
if (( ${GRAPH} || ${RENDER} || ${XLSM} || ${VIDEO} )) ; then
  LISTDIRS=()
  LISTNAMES=""
  PDFNAMES=""
  for ((i=0;i<NUMTESTS;i++)) ; do
    NAME=${EXPERIMENTS[$i,name]}
    CSV=$( getCsvFilename $NAME )
    if [ "${PDFNAMES}" == "" ] ; then PDFNAMES=${OUTDIR}/$( printf "F%03d" ${FRAMECOUNT} )_${NAME}; else PDFNAMES+="_vs_${NAME}"; fi
    LISTDIRS+=( ${OUTDIR}/${NAME}/$( printf "F%03d" ${FRAMECOUNT} ) )
    LISTNAMES+=( ${NAME} )
    LISTCSV+=( ${CSV} )
  done

  # Create xlsm
  if (( ${XLSM} )) ; then
    for ((i=0;i<NUMTESTS-1;i++)) ; do
      for ((j=i+1;j<NUMTESTS;j++)) ; do
        FILE=${OUTDIR}/$( printf "F%03d" ${FRAMECOUNT} )_${EXPERIMENTS[$i,name]}_vs_${EXPERIMENTS[$j,name]}.xlsm
        if [ ! -f $FILE ] ; then
          python3 ${CURDIR}/fill_ctc_spreadsheet.py \
            --anchor $( getCsvFilename ${EXPERIMENTS[$i,name]} ) \
            --test   $( getCsvFilename ${EXPERIMENTS[$j,name]} ) \
            --frame  $FRAMECOUNT \
            --save   $FILE
        else
          echo "${FILE} already exists"
        fi
      done
    done
  fi

  # Create graph pdf
  if (( ${GRAPH} )) ; then
    if [ ! -f ${PDFNAMES}_graphs.pdf ] ; then
      CMD="gnuplot \
        -e \"csvFiles='${LISTCSV[@]}'\" \
        -e \"names='${LISTNAMES[@]}'\" \
        -e \"output='${PDFNAMES}_graphs.pdf'\" \
        -e \"frame='${FRAMECOUNT}'\" \
        ./scripts/draw_graphs.plt "
      formatCmd  "${CMD}" "-e "
      if ! eval $CMD ; then echo "ERROR: gnuplot graphs return !0"; fi
    else
      echo "${PDFNAMES}_graphs.pdf already exists"
    fi
  fi

  # Create graph pdf
  if (( ${GRAPH} )) ; then
    if [ ! -f ${PDFNAMES}_graphs_eb.pdf ] ; then
      CMD="gnuplot \
        -e \"csvFiles='${LISTCSV[@]}'\" \
        -e \"names='${LISTNAMES[@]}'\" \
        -e \"output='${PDFNAMES}_graphs_eb.pdf'\" \
        -e \"frame='${FRAMECOUNT}'\" \
        ./scripts/draw_graphs_eb.plt "
      formatCmd  "${CMD}" "-e "
      if ! eval $CMD ; then echo "ERROR: gnuplot graphs return !0"; fi
    else
      echo "${PDFNAMES}_graphs_eb.pdf already exists"
    fi
  fi

  # Create render pdf
  if (( ${RENDER} )) ; then
    if [ ! -f ${PDFNAMES}_render.pdf ] ; then
      CMD="gnuplot \
        -e \"experiments='${LISTDIRS[@]}'\" \
        -e \"names='${LISTNAMES[@]}'\" \
        -e \"output='${PDFNAMES}_render.pdf'\" \
        -e \"frame='${FRAMECOUNT}'\" \
        ./scripts/draw_render.plt "
      formatCmd  "${CMD}" "-e "
      if ! eval $CMD ; then echo "ERROR: gnuplot render return !0"; fi
    else
      echo "${PDFNAMES}_render.pdf already exists"
    fi
  fi

  # Create comparison videos
  if (( ${VIDEO} )) ; then
    if ! command -v ${FFMPEG} &> /dev/null ; then
      if [ ! -f "${FFMPEG}" ] ; then
        print_usage "${FFMPEG} not exist, please add \--ffmpeg path_to_ffmpeg\" ";
      fi
    fi
    for ((i=0;i<NUMTESTS;i++)) ; do
      NAME=${EXPERIMENTS[$i,name]}
      DIR=${OUTDIR}/${NAME}/$( printf "F%03d" ${FRAMECOUNT} )/video
      CSV=$( getCsvFilename $NAME )
      for SEQID in $SEQS; do
        # WIDTH=480; HEIGHT=1080; Y=0; X=( 600 600 700 600 800 400 800 800 );
        WIDTH=320; HEIGHT=1080; Y=0; X=( 680 680 780 680 880 480 880 880 880 ); 
        CROP="crop=${WIDTH}:${HEIGHT}:${X[$((SEQID-1))]}:${Y}"
        
        # Get lossless video      
        CONDID=0;
        RATEID=0;
        if isNotFinish ; then
          CREATEVIDEO=0
        else
          RESULTS=( $( cat $CSV | grep "$SEQID,$CONDID,$RATEID," | \
                        awk -F ',' '{ printf("%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s ", \
                        $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16 ) }' ) )
          RATE=$(awk "BEGIN {printf \"%.2f\",${RESULTS[4]}*30/(${FRAMECOUNT}*1000000)}")
          LOSSLESSVIDEO=${DIR}/s${SEQID}c${CONDID}_${SEQNAME[$((SEQID-1))]}_x265lossless.mp4
          LOSSLESSTEXT=$(printf "CO(%.2f Mbps)\n%s dB|%s dB" ${RATE} ${RESULTS[10]} ${RESULTS[12]} )
          printf "%-30s Rate = %8.2f D1 = %8s Luma = %8s \n" ${LOSSLESSVIDEO} ${RATE} ${RESULTS[10]} ${RESULTS[12]}
        fi

        for CONDID in 1 2 ; do
          OUTVIDEO=${DIR}/s${SEQID}c${CONDID}_${SEQNAME[$((SEQID-1))]}_comparison_x265lossy.mp4
          if [ ! -f ${OUTVIDEO} ]; then
            CREATEVIDEO=1
            VIDEOS=( "${LOSSLESSVIDEO}" )
            TEXTS=( "${LOSSLESSTEXT}" )
            for RATEID in 1 2 3 4 5; do
              if isNotFinish ; then
                CREATEVIDEO=0
              else
                FILE=${DIR}/s${SEQID}c${CONDID}r${RATEID}_${SEQNAME[$((SEQID-1))]}_x265lossless.mp4
                RESULTS=( $( cat $CSV | grep "$SEQID,$CONDID,$RATEID," | \
                             awk -F ',' '{ printf("%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s ", \
                             $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16 ) }' ) )
                RATE=$(awk "BEGIN {printf \"%.2f\",${RESULTS[4]}*30/(${FRAMECOUNT}*1000000)}")
                TEXT=$(printf "R%d(%.2f Mbps)\n%.2f dB|%.2f dB" ${RATEID} ${RATE} ${RESULTS[10]} ${RESULTS[12]} )
                VIDEOS+=( "${FILE}" )
                TEXTS+=( "$TEXT" )
                printf "%-30s Rate = %8.2f D1 = %8.2f Luma = %8.2f \n" ${FILE} ${RATE} ${RESULTS[10]} ${RESULTS[12]}
              fi
            done
            if (( ${CREATEVIDEO} )); then
              TEXTPARAM="drawtext=fontcolor=white:fontsize=30:box=1:boxcolor=black@0.5:boxborderw=4:x=(w-tw)/2:y=h-th:text="
              ${FFMPEG} \
                -i ${VIDEOS[0]} \
                -vf "                   ${CROP}[a0];[a0]${TEXTPARAM}'${TEXTS[0]}'[v0];
                     movie=${VIDEOS[1]},${CROP}[a1];[a1]${TEXTPARAM}'${TEXTS[1]}'[v1];
                     movie=${VIDEOS[2]},${CROP}[a2];[a2]${TEXTPARAM}'${TEXTS[2]}'[v2];
                     movie=${VIDEOS[3]},${CROP}[a3];[a3]${TEXTPARAM}'${TEXTS[3]}'[v3];
                     movie=${VIDEOS[4]},${CROP}[a4];[a4]${TEXTPARAM}'${TEXTS[4]}'[v4];
                     movie=${VIDEOS[5]},${CROP}[a5];[a5]${TEXTPARAM}'${TEXTS[5]}'[v5];
                     [v0][v1][v2][v3][v4][v5]hstack=inputs=6" \
                -c:v libx265 \
                -crf 10 \
                ${OUTVIDEO}
            fi
        else
          echo "${OUTVIDEO} already exists"
        fi
      done #SEQID
    done #CONDID
  done #NUMTESTS
  fi
fi