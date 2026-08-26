#!/bin/bash

CURDIR=$( cd "$( dirname "$0" )" && pwd ); 

SOFT=$1;

if [ ! -f "$SOFT" ]
then 
  echo "usage: $0 [path to encode/decode sw]"
  echo " "
  echo "  $0 ./build/Release/bin/encode"
  echo "  $0 ./build/Release/bin/decode"
  echo "  $0 ../build/Release/bin/encode  > ./README.enc_params.md"
  echo "  $0 ../build/Release/bin/decode  > ./README.dec_params.md"
  echo "  $0 ../build/Release/bin/metrics > ./README.met_params.md"
  echo " "
  echo "input parameter not corrects"
  exit 
fi

${SOFT} > tmp.txt
NAME=$(basename $SOFT)
sed -i '/usage:/,$!d' tmp.txt # remove header
sed -i 1d  tmp.txt            # remove header 
sed -i '/---/d' tmp.txt

isIn=0;

echo ""
echo "<!--- ${NAME^} software input paramters --->" 
echo ""
echo "<!--- "
echo "  Note: "
echo ""
echo "        This file has been created automatically by "
echo "        $0 script. "
echo "        Please re-run this script whereas edit this file"
echo ""
echo "--->"
echo ""
if [ $NAME == encode ]
then   
  echo "# Main software input parameters"    
  echo ""
  echo "The following subsections contain input parameters for encoding, decoding, and metrics software."
  echo ""
fi
echo "## ${NAME^} software input parameters"

echo "" 
echo "| **--key=value** | **Usage** | "
echo "|---|---| "
FIRST="";
SECOND="";
while read -r line;
do
  if [[ "$line" == "" ]]
  then 
    isIn=0;
  elif [[ "$line" == *"="* ]]; 
  then
    if [ "$FIRST" != "" ] ; then echo  "| $FIRST | $SECOND | "; FIRST=""; fi
    isIn=1;
    NAME=$( echo $line | awk -F "=" '{print $1 }' )
    VAL=$(  echo $line | awk -F "=" '{print $2 }' | awk '{print $1 }' ) 
    TXT=$(  echo ${line#* } )
    FIRST=${NAME}=${VAL}
    SECOND=${TXT}    
    if [ "$FIRST" == "$SECOND" ] ; then SECOND=""; fi
  else
    if [ $isIn == 0 ]
    then  
     if [ "$FIRST" != "" ] ; then echo  "| $FIRST | $SECOND | "; FIRST=""; fi     
      echo "||| "
      echo  "| **${line}** || "
    else                 
      SECOND="${SECOND} $line"
    fi
  fi 
done < tmp.txt

if [ "$FIRST" != "" ] ; then echo  "| $FIRST | $SECOND | "; FIRST=""; fi
     
rm tmp.txt

