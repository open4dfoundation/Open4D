#!/bin/bash
#
# Generate a configuration tree in $PWD from YAML files in the same
# directory.

CURDIR=$( cd "$( dirname "$0" )" && pwd ); 
MAINDIR=$( dirname ${CURDIR} )

function formatCmd(){ 
  local f=${1}; 
  for s in ${2} ; do 
    if [ $s == "--" ] ; then f=${f//${s}/ \\\\\\n    ${s}};  else f=${f// ${s}/ \\\\\\n   ${s}};  fi
  done
  echo -e "$f" | sed 's/ *\\$/ \\/' 
}

OUTDIR="generatedConfigFiles"
CFGDIR=${MAINDIR}/cfg/
USE_RA=0
EXTRA_ARGS=()

function print_usage() {
  echo "$0 Generate configurations files "
  echo "";
  echo "  Usage:"   
  echo "    -h|--help   : print help ";
  echo "    -c|--cfgdir : configured directory    (default: \"$CFGDIR\" )";
  echo "    -o|--outdir : output directory        (default: \"$OUTDIR\" )";
  echo "    -- A B C    : extra arguments         (default: \"$OUTDIR\" )";
  echo "";
  echo "  Examples:";
  echo "    $0 -h"; 
  echo "    $0 --outdir=generatedConfigFiles --cfgdir=./cfg/ ";
  echo "    $0 --outdir=generatedConfigFiles --cfgdir=./cfg/ -- arg1 arg2 arg3";
  echo "    ";
  if [ "$#" != 0 ] ; then echo -e "ERROR: $1 \n"; fi
  exit 0;
}

# Parse input parameters
while [[ $# -gt 0 ]] ; do  
  C=$1; if [[ "$C" =~ [=] ]] ; then V=${C#*=}; elif [[ $2 == -* ]] ; then  V=""; else V=$2; shift; fi;
  case "$C" in    
    -h|--help    ) print_usage;;
    -o|--outdir* ) OUTDIR=$V;; 
    -c|--cfgdir* ) CFGDIR=$V;; 
    --usera      ) USE_RA=1;;
		--           ) EXTRA_ARGS=$@; break;;
    *            ) print_usage "unsupported arguments: $C ";;
  esac
  shift;
done

##	
# NB: it is important that the configs in each config set are
# capable of being merged together by gen-cfg.pl.  Ie, no two
# configs may have different definitions of one category.
cfg_all=(
	cfg-cond-ai-ll.yaml
	cfg-cond-ai.yaml
	cfg-cond-ld.yaml
	informative-liftingQP.yaml
)
if [ $USE_RA != 0 ] ; then cfg_all[2]=cfg-cond-ra.yaml; fi

# Check parameters
if [ ! -d $CFGDIR  ] ; then print_usage "Config directory is not valid (${CFGDIR}) "; fi
if [ $OUTDIR == "" ] ; then print_usage "Output directory is not valid (${OUTDIR}) "; fi
[[ ${CFGDIR:${#CFGDIR}-1:1} != "/" ]] && CFGDIR+="/"; 
for FILE in ${cfg_all[@]} cfg-site-default.yaml cfg-tools.yaml sequences.yaml cfg-site.yaml
do
	if [ ! -f ${CFGDIR}${FILE} ] ; then print_usage "Yaml config is not valid (${CFGDIR}${FILE}) "; fi
done 

do_one_cfgset() {
	local what=$1
	mkdir -p "${OUTDIR}"
	CFGSET="cfg_${what}[@]"
	CONDCFG=(); for file in ${!CFGSET} ; do CONDCFG+=(${CFGDIR}$file); done

	# NB: specifying extra_args at the end does not affect option
	# processing since gen-cfg.pl is flexible in argument positions
	CMD="${CURDIR}/gen-cfg.pl \
		--prefix=${OUTDIR} \
		--no-skip-sequences-without-src \
		${CFGDIR}cfg-site-default.yaml \
		${CFGDIR}cfg-tools.yaml \
		${CONDCFG[@]} \
		${CFGDIR}sequences.yaml \
		${CFGDIR}cfg-site.yaml \
		${EXTRA_ARGS[@]}"
	if ! eval $CMD ; then echo "ERROR: ${MAINDIR}/scripts/gen-cfg.pl return !0"; exit; fi
  
	rm -f "${OUTDIR}/config-merged.yaml"
}

do_one_cfgset "all"
