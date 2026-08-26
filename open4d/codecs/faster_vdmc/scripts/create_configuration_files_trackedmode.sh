#!/bin/bash

CURDIR=$( cd "$( dirname "$0" )" && pwd );
MAINDIR=$( dirname ${CURDIR} )
echo -e "\033[0;32mCreate configuration files: ${CURDIR} \033[0m";

OUTDIR="generatedConfigFiles";
SEQDIR=""
UPDATE=0
CODEC="hm"
print_usage()
{
  echo "$0 Create configuration files: "
  echo "";
  echo "  Usage:"
  echo "    -o|--outdir=: configured directory      (default: $OUTDIR )"
  echo "    -s|--seqdir=: source sequence directory (default: $SEQDIR )"
  echo "    -c|--codec=:  video codec: hm, vtm, vvf "
  echo "                      vvm, vvs              (default: $CODEC )"
  echo "";
  echo "  Examples:";
  echo "    $0  ";
  echo "    $0 \\";
  echo "      --outdir=generatedConfigFiles/hm/  \\";
  echo "      --seqdir=/home/library24/PCC/contents/mpeg_vmesh_cfp_final/contents/voxelized/ \\";
  echo "      --codec=hm";
  echo "    $0 \\";
  echo "      --outdir=generatedConfigFiles/vtm/ \\";
  echo "      --seqdir=/home/library24/PCC/contents/mpeg_vmesh_cfp_final/contents/voxelized/ \\";
  echo "      --codec=vtm";
  echo "    ";
  if [ "$#" != 0 ] ; then echo -e "ERROR: $1 \n"; fi
  exit 0;
}
while [[ $# -gt 0 ]] ; do
  C=$1; if [[ "$C" =~ [=] ]] ; then V=${C#*=}; elif [[ $2 == -* ]] ; then  V=""; else V=$2; shift; fi;
  case "$C" in
    -o|--outdir=* ) OUTDIR=$V;;
    -s|--seqdir=* ) SEQDIR=$V;;
    -c|--codec=*  ) CODEC=$V;;
    --update      ) UPDATE=1;;
    -h|--help     ) print_usage ;;
    *             ) print_usage "unsupported arguments: $C ";;
  esac
  shift;
done

if [ "${OUTDIR}" != "" ] ; then mkdir -p ${OUTDIR}; fi
if [ $UPDATE == 1 ]
then
  cd ${MAINDIR}
  CFGDIR=./cfg
  OUTDIR=${CFGDIR}/vmesh/
  MAINDIR=/path/to/mpeg-vmesh-tm
  SEQDIR=/path/to/contents
else
  if [ "$SEQDIR" == "" ] || [ ! -d ${SEQDIR} ] ; then print_usage "SEQDIR = \"${SEQDIR}\" not exists"; fi
  OUTDIR=$( cd "$OUTDIR" && pwd )
  SEQDIR=$( cd "$SEQDIR" && pwd )
  # Update windows path
  MAINDIR=${MAINDIR/\/c\//C:\/}
  SEQDIR=${SEQDIR/\/c\//C:\/}
  CFGDIR=${MAINDIR}/cfg
fi
case ${CODEC} in
  hm)
    DISP_AI=hm/displacements-ai.cfg
    DISP_LD=hm/displacements-ld.cfg
    TEXT_AI=hm/texture-ai.cfg
    TEXT_LD=hm/texture-ld.cfg
    TEXT_AI_LL=hm/texture-ai-ll.cfg
    TEXT_LD_LL=hm/texture-ld-ll.cfg
    ;;
  vtm)
    DISP_AI=vtm/displacements-ai.cfg
    DISP_LD=vtm/displacements-ld.cfg
    TEXT_AI=vtm/texture-ai.cfg
    TEXT_LD=vtm/texture-ld.cfg
    ;;
  vvf)
    DISP_AI=vtm/displacements-ai.cfg
    DISP_LD=vtm/displacements-ld.cfg
    TEXT_AI=vvenc/texture-ai-faster.cfg
    TEXT_LD=vvenc/texture-ra-faster.cfg
    ;;
  vvm)
    DISP_AI=vtm/displacements-ai.cfg
    DISP_LD=vtm/displacements-ld.cfg
    TEXT_AI=vvenc/texture-ai-medium.cfg
    TEXT_LD=vvenc/texture-ra-medium.cfg
    ;;
  vvs)
    DISP_AI=vtm/displacements-ai.cfg
    DISP_LD=vtm/displacements-ld.cfg
    TEXT_AI=vvenc/texture-ai-slower.cfg
    TEXT_LD=vvenc/texture-ra-slower.cfg
    ;;
  *) print_usage "CODEC = \"${CODEC}\" not corrects";;
esac

# Update cfg-site.yaml file
echo -e "\033[0;32mUpdate cfg-site.yaml file: ${CURDIR} \033[0m";
CFGSITE=${CFGDIR}/cfg-site.yaml
rm ${CFGSITE} -f
cat << EOF >> ${CFGSITE}
# Site-specific configuration
---

vars:
  # this is the directory containing all the source sequences
  seq-prefix: ${SEQDIR}/

  # this is the directory containing the hm/hdrtools config
  cfg-prefix: ${MAINDIR}/cfg/

  # video configuration files
  video-config-disp-ai: ${DISP_AI}
  video-config-disp-ld: ${DISP_LD}
  video-config-text-ai: ${TEXT_AI}
  video-config-text-ld: ${TEXT_LD}
  video-config-text-ai-ll: ${TEXT_AI_LL}
  video-config-text-ld-ll: ${TEXT_LD_LL}

EOF

cat $CFGSITE

# Generate configurations
if [ "$( command -v singularity )" == "" ]
then
  ${CURDIR}/gen-cfg-trackedmode.sh --cfgdir=${CFGDIR} --outdir=${OUTDIR}
else
  singularity exec --bind /home/scratch03,/home/wp21 /home/isl_tools/SHUB/tools/perl5.30 \
    ${CURDIR}/gen-cfg.sh --cfgdir=${CFGDIR} --outdir=${OUTDIR}
fi

exit
