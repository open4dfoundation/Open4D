#!/bin/bash

CURDIR=$( cd "$( dirname "$0" )" && pwd );
MAINDIR=$( dirname ${CURDIR} )
echo -e "\033[0;32mBuild external tools: mpeg-pcc-mmetric, mpeg-pcc-renderer, HM and HDRTools \033[0m";

ALL=0
HM=0
print_usage()
{
  echo "$0Clone and build external tools used by run.sh script and unit tests: "
  echo "";
  echo "  Usage:"
  echo "    --all: clone HM and HDRTools for unit tests (default: $ALL)"
  echo "";
  echo "  Examples:";
  echo "    $0  ";
  echo "    $0 --HM: clone HM for MCTS extraction";
  echo "    $0 --all";
  echo "";
  if [ "$#" != 0 ] ; then echo -e "ERROR: $# $1 \n"; fi
  exit 0;
}
while [[ $# -gt 0 ]] ; do
  C=$1; if [[ "$C" =~ [=] ]] ; then V=${C#*=}; elif [[ $2 == -* ]] ; then  V=""; else V=$2; shift; fi;
  case "$C" in
    --all    ) ALL=1;;
    --HM    ) HM=1;;
    -h|--help) print_usage ;;
    *        ) print_usage "unsupported arguments: $C ";;
  esac
  shift;
done

DEPDIR=${CURDIR}/../externaltools
case "$(uname -s)" in
  Linux*)     MACHINE=Linux;;
  Darwin*)    MACHINE=Mac;;
  CYGWIN*)    MACHINE=Cygwin;;
  MINGW*)     MACHINE=MinGw;;
  MSYS*)      MACHINE=MSys;;
  *)          MACHINE="UNKNOWN:$(uname -s)"
esac

CMAKE=cmake;
if [ "$( cmake  --version 2>&1 | grep version | awk '{print $3 }' | awk -F '.' '{print $1}' )" == 3 ] ; then CMAKE=cmake; fi
if [ "$( cmake3 --version 2>&1 | grep version | awk '{print $3 }' | awk -F '.' '{print $1}' )" == 3 ] ; then CMAKE=cmake3; fi
if [ "$CMAKE" == "" ] ; then echo "Can't find cmake > 3.0"; exit; fi
if [ "$MACHINE" == "Linux" ] ; then NUMBER_OF_PROCESSORS=$( grep -c ^processor /proc/cpuinfo ); fi

# mpeg-pcc-mmetric
MMETRIC=${DEPDIR}/mpeg-pcc-mmetric
if [ ! -d ${MMETRIC} ]
then
  FILE=${MAINDIR}/dependencies/cmake/geometry/mmetric.cmake
  URL=$( cat ${FILE} | grep GIT_REPOSITORY | awk '{print $2}' )
  TAG=$( cat ${FILE} | grep GIT_TAG        | awk '{print $2}' )
  echo -e "\033[0;32mClone: ${MMETRIC} url = ${URL} tag = ${TAG} \033[0m";
  git clone ${URL} -b ${TAG} ${MMETRIC}
fi
echo -e "\033[0;32mBuild: ${MMETRIC} \033[0m";
${MMETRIC}/build.sh

# mpeg-pcc-render
RENDER=${DEPDIR}/mpeg-pcc-renderer
if [ ! -d ${RENDER} ]
then
  echo -e "\033[0;32mClone: ${RENDER} \033[0m";
  git clone https://github.com/MPEGGroup/mpeg-3dg-renderer.git ${RENDER}
fi
echo -e "\033[0;32mBuild: ${RENDER} \033[0m";
${RENDER}/build.sh noomp

if [ ${ALL} == 1 ] || [ ${HM} == 1 ]
then
  # HM-16.21+SCM-8.8
  HMDIR=${DEPDIR}/hm-16.21+scm-8.8
  if [ ! -d ${HMDIR} ]
  then
    echo -e "\033[0;32mClone: ${HMDIR} \033[0m";
    git clone https://vcgit.hhi.fraunhofer.de/jvet/HM.git -b HM-16.21+SCM-8.8 ${HMDIR}
  fi
  echo -e "\033[0;32mBuild: ${HMDIR} \033[0m";

  sed -i  's/-Wno-unknown-attributes/-Wno-unused-but-set-variable -Wno-unknown-attributes -Wno-error/' \
    ${HMDIR}/CMakeLists.txt
  ${CMAKE} -B ${HMDIR}/build_dir ${HMDIR}/
  ${CMAKE} --build ${HMDIR}/build_dir --config Release --parallel ${NUMBER_OF_PROCESSORS}
fi

if [ ${ALL} == 1 ]
then
  # HDRTOOLS
  HDRTOOLSDIR=${DEPDIR}/hdrtools
  if [ ! -d ${HDRTOOLSDIR} ]
  then
    echo -e "\033[0;32mClone: ${HDRTOOLSDIR} \033[0m";
    git clone https://gitlab.com/standards/HDRTools.git -b master ${HDRTOOLSDIR};
  fi

  if [ "${MACHINE}" != "Linux" ] && [ "${MACHINE}" != "Mac" ]
  then
    # clone and build zlib
    ZLIBDIR=${DEPDIR}/zlib
    echo -e "\033[0;32mDonwload and build: ${ZLIBDIR} \033[0m";
    if [ ! -f ${ZLIBDIR}/lib/zlib.lib ]
    then
      rm -rf ${ZLIBDIR}/
      mkdir ${ZLIBDIR}
      curl https://zlib.net/zlib1212.zip -L -o ${ZLIBDIR}/zlib1212.zip
      unzip -q -o ${ZLIBDIR}/zlib1212.zip -d ${ZLIBDIR}
      ${CMAKE} -B ${ZLIBDIR}/build ${ZLIBDIR}/zlib-1.2.12/ -DCMAKE_INSTALL_PREFIX=${ZLIBDIR}/
      ${CMAKE} --build ${ZLIBDIR}/build/ --config Release --target install --parallel ${NUMBER_OF_PROCESSORS}
      rm -rf ${ZLIBDIR}/zlib1212.zip ${ZLIBDIR}/build/ ${ZLIBDIR}/zlib-1.2.12/
    fi

    echo -e "\033[0;32mBuild: ${HDRTOOLSDIR} \033[0m";
    # Build HdrTools
    ${CMAKE} -B ${HDRTOOLSDIR}/build ${HDRTOOLSDIR}  \
      -DCMAKE_BUILD_TYPE=Release  \
      -DLIBPNG=ON  \
      -DZLIB_LIBRARY=${ZLIBDIR}/lib/zlib.lib  \
      -DZLIB_INCLUDE_DIR=${ZLIBDIR}/include/

    ${CMAKE} --build ${HDRTOOLSDIR}/build/ --config Release -j ${NUMBER_OF_PROCESSORS}

    cp -f ${ZLIBDIR}/bin/zlib.dll ${HDRTOOLSDIR}/build/bin/Release/
  else
    echo -e "\033[0;32mBuild: ${HDRTOOLSDIR} \033[0m";
    ${CMAKE} -B ${HDRTOOLSDIR}/build/ ${HDRTOOLSDIR} \
      -DCMAKE_BUILD_TYPE=Release \
      -DLIBPNG=ON
    ${CMAKE} --build ${HDRTOOLSDIR}/build/ --config Release -j ${NUMBER_OF_PROCESSORS}
  fi
fi

