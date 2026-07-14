#!/bin/bash

for meshname in /home/sp53252/nmc/objs_original/*.obj; do
    meshname=$(basename "$meshname" .obj)
    echo "Processing $meshname"
    python compress.py "$meshname" -ns 3 -cs 5000 -hd 28 -nl 17
done