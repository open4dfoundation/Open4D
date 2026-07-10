#!/bin/bash

export DOTNET_ROOT="$HOME/.dotnet"
export PATH="$HOME/.dotnet:$PATH"

DATASET="..."
NUM_FRAMES=10
NUM_CENTERS=2000

for GROUP_IDX in {1..10}; do
    echo "======================"
    echo " Running group $GROUP_IDX"
    echo "======================"

    cd tsmc/

    # --- Step 1: Get reference center ---
    python ./get_reference_center.py \
        --dataset $DATASET --num_frames $NUM_FRAMES --num_centers $NUM_CENTERS \
        --centers_dir ../arap-volume-tracking/data/combined-100-max-2000 \
        --group_idx $GROUP_IDX
       # --random_state 19056

    # --- Step 2: Get transformation ---
    python ./get_transformation.py \
        --dataset $DATASET --num_frames $NUM_FRAMES --num_centers $NUM_CENTERS \
        --centers_dir ../arap-volume-tracking/data/combined-100-max-2000 \
        --firstIndex 0 --lastIndex 9 \
        --group_idx $GROUP_IDX

    # --- Step 3: Run TVMEditor (stage 1) ---
    cd ..
    cd tvm-editing/
    TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test $DATASET 1 0 9 \
        "./TVMEditor.Test/bin/Release/net5.0/Data/${DATASET}_2000/" \
        "./TVMEditor.Test/bin/Release/net5.0/output/${DATASET}_2000/"
    cd ..

    # --- Step 4: Extract reference mesh ---
    cd tsmc/
    python ./extract_reference_mesh.py \
        --dataset $DATASET --num_frames $NUM_FRAMES --num_centers $NUM_CENTERS \
        --inputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/${DATASET}_2000/output/ \
        --outputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/${DATASET}_2000/reference_mesh/ \
        --firstIndex 0 --lastIndex 9 --key 4
    cd ..

    # --- Step 5: Run TVMEditor (stage 2) ---
    cd tvm-editing/
    TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test $DATASET 2 0 9 \
        "./TVMEditor.Test/bin/Release/net5.0/Data/${DATASET}_2000" \
        "./TVMEditor.Test/bin/Release/net5.0/output/${DATASET}_2000"
    cd ..

    # --- Step 6: Displacements ---
    cd tsmc/
    python ./get_displacements.py \
        --dataset $DATASET --num_frames $NUM_FRAMES --num_centers $NUM_CENTERS \
        --target_mesh_path ../arap-volume-tracking/data/combined_scaled \
        --firstIndex 0 --lastIndex 9 \
        --group_idx $GROUP_IDX

    # --- Step 7: Compress displacements ---
    python compress_displacements.py \
        --dataset $DATASET --num_frames $NUM_FRAMES --num_eigenvectors 5 \
        --displacement_path ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/${DATASET}_2000/reference \
        --output_path ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/${DATASET}_2000/reference \
        --firstIndex 0 --lastIndex 9 \
        --reference_mesh_path ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/${DATASET}_2000/reference_mesh/others/decoded_decimated_reference_mesh.obj

    # --- Step 8: Evaluation ---
    python evaluation.py \
        --dataset $DATASET --num_frames $NUM_FRAMES --num_centers $NUM_CENTERS \
        --input_path ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/${DATASET}_2000/reference \
        --dynamic_static_path ../data/$DATASET/meshes \
        --firstIndex 0 --lastIndex 9 \
        --reference_mesh_path ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/${DATASET}_2000/reference_mesh/others/decoded_decimated_reference_mesh.obj \
        --group_idx $GROUP_IDX

    cd ..

done
