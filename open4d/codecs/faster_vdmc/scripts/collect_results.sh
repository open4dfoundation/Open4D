#!/bin/bash

CURDIR=$( cd "$( dirname "$0" )" && pwd );
MAINDIR=$( dirname ${CURDIR} )

function print_usage() {
  echo "$0 Collect results from log files"
  echo "";
  echo "  Usage:"
  echo "    -h|--help : print help ";
  echo "    -q|--quiet: disable logs         (default: $VERBOSE )";
  echo "    --condId= : condition: 0, 1, 2   (default: $CONDID )";
  echo "    --seqId=  : seq: 1,2,3,4,5,6,7,8 (default: $SEQID )";
  echo "    --rateId= : Rate: 0,1,2,3,4,5    (default: $RATEID )";
  echo "    --vdmc    : vdmc bitstream file  (default: \"$VDMC\" )";
  echo "    --logenc  : encoder log file     (default: \"$LOGENC\" )";
  echo "    --logdec  : decoder log file     (default: \"$LOGDEC\" )";
  echo "    --logmet  : metrics log file     (default: \"$LOGMET\" )";
  echo "    --csv     : generate .csv file   (default: \"$CSV\" )";
  echo "";
  echo "  Examples:";
  echo "    $0 -h";
  echo "    $0 \\"
  echo "      --condId=1 \\"
  echo "      --seqId=3 \\"
  echo "      --rateId=3 \\"
  echo "      --vdmc=test.bin \\"
  echo "      --logenc=encoder.log \\"
  echo "      --logdec=decoder.log \\"
  echo "      --logmet=metric.log ";
  echo "    ";
  if [ "$#" != 0 ] ; then echo -e "ERROR: $1 \n"; fi
  exit 0;
}

# Set default parameters
VERBOSE=1
CONDID=1
SEQID=1
RATEID=1
VDMC=
LOGENC=
LOGDEC=
LOGMET=
CSV=

# Parse input parameters
while [[ $# -gt 0 ]] ; do
  C=$1; if [[ "$C" =~ [=] ]] ; then V=${C#*=}; elif [[ $2 == -* ]] ; then  V=""; else V=$2; shift; fi;
  case "$C" in
    -h|--help  ) print_usage;;
    -q|--quiet ) VERBOSE=0;;
    --condId   ) CONDID=$V;;
    --seqId    ) SEQID=$V;;
    --rateId   ) RATEID=$V;;
    --vdmc     ) VDMC=$V;;
    --logenc   ) LOGENC=$V;;
    --logdec   ) LOGDEC=$V;;
    --logmet   ) LOGMET=$V;;
    --csv      ) CSV=$V;;
    *          ) print_usage "unsupported arguments: $C ";;
  esac
  shift;
done

# Check parameters
if [ ! -f "$VDMC"   ] ; then print_usage "vdmc = \"$VDMC\" not exists"; exit -1; fi
if [ ! -f "$LOGENC" ] ; then print_usage "logenc = \"$LOGENC\" not exists"; exit -1; fi
if [ ! -f "$LOGDEC" ] ; then print_usage "logdec = \"$LOGDEC\" not exists"; exit -1; fi
if [ ! -f "$LOGMET" ] ; then print_usage "logmet = \"$LOGMET\" not exists"; exit -1; fi

# Get number of output faces
NBOUTPUTFACES=$( cat ${LOGENC} | grep "Sequence face count" | awk '{print $4}' )

# Get bitstream total size in bits as reported by the encoding process
TOTALSIZEBITS=$(stat -c '%s' "${VDMC}")
TOTALSIZEBITS=$((TOTALSIZEBITS * 8))

# Get partial sizes of substreams
BASEMESHINTRASIZEBITS=$( cat ${LOGENC} | grep 'V3C_BMD.*Header' | awk '{print $16 * 8}' )
BASEMESHINTERSIZEBITS=$( cat ${LOGENC} | grep 'V3C_BMD.*Header' | awk '{print $21 * 8}' )
DISPLACEMENTSIZEBITS=$( cat ${LOGENC} | grep "Displacement:" | awk '{print $2 * 8}' )
ATTRVIDEOSIZEBITS=$( cat ${LOGENC} | grep "Attribute:" | awk '{print $2 * 8}' )
PACKVIDEOSIZEBITS=$( cat ${LOGENC} | grep "Packed:" | awk '{print $2 * 8}' )
METADATASIZEBITS=$( cat ${LOGENC} | grep "Metadata:" | awk '{print $2 * 8}' )
# Combine Packed Video with Attribute - no possibility to separate geometry and video for now
ATTRVIDEOSIZEBITS=$(($ATTRVIDEOSIZEBITS + $PACKVIDEOSIZEBITS))
# Check if video attributes are coded (RECHECK with PVD)
if [ ${ATTRVIDEOSIZEBITS} == 0 ]; then
  NOVIDEOATTR=1;
else
  NOVIDEOATTR=0;
fi

if [ $ATTRVIDEOSIZEBITS -eq 0 ] && [ $DISPLACEMENTSIZEBITS -eq 0 ] && [ $PACKVIDEOSIZEBITS -ne 0 ]; then
    ATTRVIDEOSIZEBITS=$PACKVIDEOSIZEBITS
fi

# Get user encoder and decoder runtimes
ENCTIME=$( cat ${LOGENC} | grep "Sequence processing time" | awk '{print $4}' )
DECTIME=$( cat ${LOGDEC} | grep "Sequence processing time" | awk '{print $4}' )
ENCMEMO=$( cat ${LOGENC} | grep "Sequence peak memory"     | awk '{print $4 / 1024. }' )
DECMEMO=$( cat ${LOGDEC} | grep "Sequence peak memory"     | awk '{print $4 / 1024. }' )

# Get edge breaker timings
# $1 name of file to analyse, $2 prefix
get_value ( ) {
	# search lines with prefix, then remove the prefix, then keep only first word and sumup
	TMPGREP=$(cat $1 | grep "$2")
	if [ -z "$TMPGREP" ]; then
		VALUE=0
	else
		VALUE=$(echo "$TMPGREP" | sed "s/^$2//" | awk 'NR == 1 { sum=0 } { sum+=$1; } END {printf "%f\n", sum/NR}')
	fi
}

# Get edge breaker timings
# $1 name of file to analyse, $2 prefix
get_value ( ) {
	# search lines with prefix, then remove the prefix, then keep only first word and sumup
	VALUE=0
	VMIN=0
	VMAX=0
	TMPGREP=$(cat $1 | grep "$2")
	if [ ! -z "$TMPGREP" ]; then
		STRING=$(echo "$TMPGREP" | sed "s/^$2//" | awk 'NR == 1 { sum=0; vmin=1000000; vmax=0; } { sum+=$1; vmin=($1 < vmin) ? $1 : vmin; vmax=($1 > vmax) ? $1 : vmax; } END {printf "%f %f %f\n", sum/NR, vmin, vmax}')
		IFS=' ' #setting space as delimiter  
		read -ra STRARR <<<"$STRING" #reading STRING as an array as tokens separated by IFS  
		VALUE=${STRARR[0]}
		VMIN=${STRARR[1]}
		VMAX=${STRARR[2]}
	fi
}

# Base Mesh timings (not precise due to tic/toc and overestimated in profile mode (xN) )
get_value ${LOGENC} "Duration baseMeshEncode           : time =     "
BM_ENCTIME=$VALUE
get_value ${LOGDEC} "Duration baseMeshDecode           : time =     "
BM_DECTIME=$VALUE

# EB detailed profiling
get_value ${LOGENC} "EB  FaceId bytes = "
EB_ENC_FIDSIZE=$VALUE
get_value ${LOGENC} "EB  Positions bytes = "
EB_ENC_POSSIZE=$VALUE
get_value ${LOGENC} "EB  UVCoords bytes = "
EB_ENC_UVSIZE=$VALUE
get_value ${LOGENC} "EB  UVCoords auxiliary orientation selection bytes = "
EB_ENC_UVORISIZE=$VALUE
get_value ${LOGENC} "EB  UVCoords auxiliary seam bytes = "
EB_ENC_UVSEAMSIZE=$VALUE
get_value ${LOGENC} "EB  Topology bytes = "
EB_ENC_TOPOSIZE=$VALUE
get_value ${LOGENC} "EB  Handles auxiliary orientation selection bytes = "
EB_ENC_HANDLESIZE=$VALUE
get_value ${LOGENC} "EB  Deduplicate indices bytes = "
EB_ENC_DEDUPSIZE=$VALUE
get_value ${LOGENC} "EB  Total payload bytes = "
EB_ENC_TOTALSIZE=$VALUE

get_value ${LOGENC} "EB  Total encoding time (ms) = "
EB_ENC_TOTALTIME=$VALUE
EB_ENC_TOTALTIME_MIN=$VMIN
EB_ENC_TOTALTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Convert time (ms) = "
EB_ENC_CONVTIME=$VALUE
EB_ENC_CONVTIME_MIN=$VMIN
EB_ENC_CONVTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Hole fill time (ms) = "
EB_ENC_FILLTIME=$VALUE
EB_ENC_FILLTIME_MIN=$VMIN
EB_ENC_FILLTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Encoding time (ms) = "
EB_ENC_ENCTIME=$VALUE
EB_ENC_ENCTIME_MIN=$VMIN
EB_ENC_ENCTIME_MAX=$VMAX
get_value ${LOGENC} "EB  AC encoding time (ms) = "
EB_ENC_ACTIME=$VALUE
EB_ENC_ACTIME_MIN=$VMIN
EB_ENC_ACTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Syntax fill time (ms) = "
EB_ENC_SYNTIME=$VALUE
EB_ENC_SYNTIME_MIN=$VMIN
EB_ENC_SYNTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Serialize syntax to bitstream time (ms) = "
EB_ENC_SERTIME=$VALUE
EB_ENC_SERTIME_MIN=$VMIN
EB_ENC_SERTIME_MAX=$VMAX

get_value ${LOGENC} "EB  Total decoding time (ms) = "
EB_DEC_TOTALTIME=$VALUE
EB_DEC_TOTALTIME_MIN=$VMIN
EB_DEC_TOTALTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Syntax read and AC decoding time (ms) = "
EB_DEC_ACTIME=$VALUE
EB_DEC_ACTIME_MIN=$VMIN
EB_DEC_ACTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Decoding time (ms) = "
EB_DEC_DECTIME=$VALUE
EB_DEC_DECTIME_MIN=$VMIN
EB_DEC_DECTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Post-reindex time (ms) = "
EB_DEC_PRTIME=$VALUE
EB_DEC_PRTIME_MIN=$VMIN
EB_DEC_PRTIME_MAX=$VMAX
get_value ${LOGENC} "EB  Cleanup dummy and convert time (ms) = "
EB_DEC_CONVTIME=$VALUE
EB_DEC_CONVTIME_MIN=$VMIN
EB_DEC_CONVTIME_MAX=$VMAX

# Get metrics results
RESULTS=( 0 0 0 0 0 0 0 );
if [ ${CONDID} == 0 ] ; then
    TEXTUREMATCH=1;
    if [ ${NOVIDEOATTR} == 0 ] && [ "$( cat ${LOGMET} | grep "texture maps are equal" )" == "" ]; then
      TEXTUREMATCH=0;
    fi
    if [ ${TEXTUREMATCH} == 0 ] || [ "$( cat ${LOGMET} | grep "meshes are equal"       )" == "" ]; then
    ENCTIME=-1;
    DECTIME=-1;
    ENCMEMO=-1;
    DECMEMO=-1;
    NBOUTPUTFACES=-1;
    TOTALSIZEBITS=-1;
    BASEMESHINTRASIZEBITS=-1;
    BASEMESHINTERSIZEBITS=-1;
    DISPLACEMENTSIZEBITS=-1;
    DISPLACEMENTSIZEBITS=-1;
    ATTRVIDEOSIZEBITS=-1;
    METADATASIZEBITS=-1;
  else
    RESULTS=( inf inf inf inf inf inf inf );
  fi
else
  if [ "$( cat ${LOGMET} | grep "mseF, PSNR(p2point) Mean=" )" == "" ] ; then
    RESULTS=( $( cat ${LOGMET} | grep "Metric_results" | awk -f ',' '{ printf("%s %s %s %s %s %s %s ", $3, $4, $5, $6, $7, $8, $9 )}') )
  else
    RESULTS[0]=$( cat ${LOGMET} | grep "mseF, PSNR(p2point) Mean="   | awk -F= '{ printf $2; }' )
    RESULTS[1]=$( cat ${LOGMET} | grep "mseF, PSNR(p2plane) Mean="   | awk -F= '{ printf $2; }' )
    RESULTS[2]=$( cat ${LOGMET} | grep "c\[0\],PSNRF          Mean=" | awk -F= '{ printf $2; }' )
    RESULTS[3]=$( cat ${LOGMET} | grep "c\[1\],PSNRF          Mean=" | awk -F= '{ printf $2; }' )
    RESULTS[4]=$( cat ${LOGMET} | grep "c\[2\],PSNRF          Mean=" | awk -F= '{ printf $2; }' )
    RESULTS[5]=$( cat ${LOGMET} | grep "GEO PSNR Mean="              | awk -F= '{ printf $2; }' )
    RESULTS[6]=$( cat ${LOGMET} | grep "Y   PSNR Mean="              | awk -F= '{ printf $2; }' )
  fi
fi
# setting non applicable results to 0.0 when no video attributes
if [ ${NOVIDEOATTR} == 1 ]; then
  RESULTS[2]=0.0;
  RESULTS[3]=0.0;
  RESULTS[4]=0.0;
  RESULTS[6]=0.0;
fi
echo ===
echo ${RESULTS[6]}
#checking for decoder mismatch
  if [ "$( cat ${LOGDEC} | grep "DIFF" )" == "" ]; then
    echo "No mismatch detected"
  else
    DECTIME=-1;
  fi

# Output results
if [ "${CSV}" != "" ]; then
  SEQLINE=( 0 1 2 3 6 7 4 5 )
  if [ ! -f ${CSV} ] ; then
    HEADER="SeqId,CondId,RateId,NbOutputFaces,TotalBitstreamBits,BaseMeshIntraBits,BaseMeshInterBits,DisplacementBits,AttrVideoBits,MetaDataBits,"
    HEADER+="GridD1,GridD2,GridLuma,GridChromaCb,GridChromaCr,IbsmGeom,IbsmLuma,"
    HEADER+="UserEncoderRuntime,UserDecoderRuntime,PeakEncoderMemory,PeakDecoderMemory\
            ,MeshEncoderRuntime\
            ,MeshDecoderRuntime\
            ,EbAvgTotalEncoderTime\
            ,EbMinTotalEncoderTime\
            ,EbMaxTotalEncoderTime\
            ,EbAvgTotalDecoderTime\
            ,EbMinTotalDecoderTime\
            ,EbMaxTotalDecoderTime\
            ,EbAvgEncConvertTime\
            ,EbMinEncConvertTime\
            ,EbMaxEncConvertTime\
            ,EbAvgEncHoleFillTime\
            ,EbMinEncHoleFillTime\
            ,EbMaxEncHoleFillTime\
            ,EbAvgEncodingTime\
            ,EbMinEncodingTime\
            ,EbMaxEncodingTime\
            ,EbAvgEncAcTime\
            ,EbMinEncAcTime\
            ,EbMaxEncAcTime\
            ,EbAvgEncSyntaxFillTime\
            ,EbMinEncSyntaxFillTime\
            ,EbMaxEncSyntaxFillTime\
            ,EbAvgEncSyntaxSerializeTime\
            ,EbMinEncSyntaxSerializeTime\
            ,EbMaxEncSyntaxSerializeTime\
            ,EbAvgDecSyntaxAcTime\
            ,EbMinDecSyntaxAcTime\
            ,EbMaxDecSyntaxAcTime\
            ,EbAvgDecodingTime\
            ,EbMinDecodingTime\
            ,EbMaxDecodingTime\
            ,EbAvgDecPostReindexTime\
            ,EbMinDecPostReindexTime\
            ,EbMaxDecPostReindexTime\
            ,EbAvgDecConvertTime\
            ,EbMinDecConvertTime\
            ,EbMaxDecConvertTime\
            ,EbEncFaceIdBytes\
            ,EbEncPosBytes\
            ,EbEncUvCoordBytes\
            ,EbEncUvOriBytes\
            ,EbEncUvSeamBytes\
            ,EbEncTopoBytes\
            ,EbEncHandleBytes\
            ,EbEncDedupBytes\
            ,EbEncTotalBytes"
    echo $HEADER > ${CSV}
  fi
  STR="${SEQID},${CONDID},${RATEID},${NBOUTPUTFACES},${TOTALSIZEBITS},"
  STR+="${BASEMESHINTRASIZEBITS},${BASEMESHINTERSIZEBITS},${DISPLACEMENTSIZEBITS},${ATTRVIDEOSIZEBITS},${METADATASIZEBITS},"
  STR+="${RESULTS[0]},${RESULTS[1]},${RESULTS[2]},${RESULTS[3]},${RESULTS[4]},${RESULTS[5]},${RESULTS[6]},"
  STR+="${ENCTIME},${DECTIME},${ENCMEMO},${DECMEMO}"
  STR+=",${BM_ENCTIME}\
        ,${BM_DECTIME}\
        ,${EB_ENC_TOTALTIME}\
        ,${EB_ENC_TOTALTIME_MIN}\
        ,${EB_ENC_TOTALTIME_MAX}\
        ,${EB_DEC_TOTALTIME}\
        ,${EB_DEC_TOTALTIME_MIN}\
        ,${EB_DEC_TOTALTIME_MAX}\
        ,${EB_ENC_CONVTIME}\
        ,${EB_ENC_CONVTIME_MIN}\
        ,${EB_ENC_CONVTIME_MAX}\
        ,${EB_ENC_FILLTIME}\
        ,${EB_ENC_FILLTIME_MIN}\
        ,${EB_ENC_FILLTIME_MAX}\
        ,${EB_ENC_ENCTIME}\
        ,${EB_ENC_ENCTIME_MIN}\
        ,${EB_ENC_ENCTIME_MAX}\
        ,${EB_ENC_ACTIME}\
        ,${EB_ENC_ACTIME_MIN}\
        ,${EB_ENC_ACTIME_MAX}\
        ,${EB_ENC_SYNTIME}\
        ,${EB_ENC_SYNTIME_MIN}\
        ,${EB_ENC_SYNTIME_MAX}\
        ,${EB_ENC_SERTIME}\
        ,${EB_ENC_SERTIME_MIN}\
        ,${EB_ENC_SERTIME_MAX}\
        ,${EB_DEC_ACTIME}\
        ,${EB_DEC_ACTIME_MIN}\
        ,${EB_DEC_ACTIME_MAX}\
        ,${EB_DEC_DECTIME}\
        ,${EB_DEC_DECTIME_MIN}\
        ,${EB_DEC_DECTIME_MAX}\
        ,${EB_DEC_PRTIME}\
        ,${EB_DEC_PRTIME_MIN}\
        ,${EB_DEC_PRTIME_MAX}\
        ,${EB_DEC_CONVTIME}\
        ,${EB_DEC_CONVTIME_MIN}\
        ,${EB_DEC_CONVTIME_MAX}\
        ,${EB_ENC_FIDSIZE}\
        ,${EB_ENC_POSSIZE}\
        ,${EB_ENC_UVSIZE}\
        ,${EB_ENC_UVORISIZE}\
        ,${EB_ENC_UVSEAMSIZE}\
        ,${EB_ENC_TOPOSIZE}\
        ,${EB_ENC_HANDLESIZE}\
        ,${EB_ENC_DEDUPSIZE}\
        ,${EB_ENC_TOTALSIZE}"
  echo $STR >> ${CSV}
  if (( $VERBOSE )) ; then
    printf "%2d %2d %2d | %9d %9d | %9.5f %9.5f %9.5f %9.5f %9.5f | %9.5f %9.5f | %9.2f %9.2f | %9.2f %9.2f | %s \n" \
        ${SEQID} ${CONDID} ${RATEID} ${NBOUTPUTFACES} ${TOTALSIZEBITS} \
        ${RESULTS[0]} ${RESULTS[1]} ${RESULTS[2]} ${RESULTS[3]} ${RESULTS[4]} ${RESULTS[5]} ${RESULTS[6]} \
        ${ENCTIME} ${DECTIME} ${ENCMEMO} ${DECMEMO} ${OUTDIR}
  fi
else
  STR="${SEQID} ${CONDID} ${RATEID} ${NBOUTPUTFACES} ${TOTALSIZEBITS} "
  STR+="${RESULTS[0]} ${RESULTS[1]} ${RESULTS[2]} ${RESULTS[3]} ${RESULTS[4]} ${RESULTS[5]} ${RESULTS[6]} "
  STR+="${ENCTIME} ${DECTIME} ${ENCMEMO} ${DECMEMO},"
   STR+=",${BM_ENCTIME}\
        ,${BM_DECTIME}\
        ,${EB_ENC_TOTALTIME}\
        ,${EB_ENC_TOTALTIME_MIN}\
        ,${EB_ENC_TOTALTIME_MAX}\
        ,${EB_DEC_TOTALTIME}\
        ,${EB_DEC_TOTALTIME_MIN}\
        ,${EB_DEC_TOTALTIME_MAX}\
        ,${EB_ENC_CONVTIME}\
        ,${EB_ENC_CONVTIME_MIN}\
        ,${EB_ENC_CONVTIME_MAX}\
        ,${EB_ENC_FILLTIME}\
        ,${EB_ENC_FILLTIME_MIN}\
        ,${EB_ENC_FILLTIME_MAX}\
        ,${EB_ENC_ENCTIME}\
        ,${EB_ENC_ENCTIME_MIN}\
        ,${EB_ENC_ENCTIME_MAX}\
        ,${EB_ENC_ACTIME}\
        ,${EB_ENC_ACTIME_MIN}\
        ,${EB_ENC_ACTIME_MAX}\
        ,${EB_ENC_SYNTIME}\
        ,${EB_ENC_SYNTIME_MIN}\
        ,${EB_ENC_SYNTIME_MAX}\
        ,${EB_ENC_SERTIME}\
        ,${EB_ENC_SERTIME_MIN}\
        ,${EB_ENC_SERTIME_MAX}\
        ,${EB_DEC_ACTIME}\
        ,${EB_DEC_ACTIME_MIN}\
        ,${EB_DEC_ACTIME_MAX}\
        ,${EB_DEC_DECTIME}\
        ,${EB_DEC_DECTIME_MIN}\
        ,${EB_DEC_DECTIME_MAX}\
        ,${EB_DEC_PRTIME}\
        ,${EB_DEC_PRTIME_MIN}\
        ,${EB_DEC_PRTIME_MAX}\
        ,${EB_DEC_CONVTIME}\
        ,${EB_DEC_CONVTIME_MIN}\
        ,${EB_DEC_CONVTIME_MAX}\
        ,${EB_ENC_FIDSIZE}\
        ,${EB_ENC_POSSIZE}\
        ,${EB_ENC_UVSIZE}\
        ,${EB_ENC_UVORISIZE}\
        ,${EB_ENC_UVSEAMSIZE}\
        ,${EB_ENC_TOPOSIZE}\
        ,${EB_ENC_HANDLESIZE}\
        ,${EB_ENC_DEDUPSIZE}\
        ,${EB_ENC_TOTALSIZE}"
  echo $STR
fi
