#!/bin/bash
# Activate Conda environment
export PATH="/opt/conda/envs/open3d_env/bin:$PATH"

# Activate the conda environment (if conda command is available in the shell)
source /opt/conda/etc/profile.d/conda.sh
conda activate open3d_env

## basketball_player

# Step 1: ARAP Volume Tracking and Global Optimization
cd ./arap-volume-tracking
dotnet build -c release
dotnet ./bin/Client.dll ./config/max/config-basketball-max.xml
dotnet ./bin/Client.dll ./config/impr/config-basketball-impr.xml

# Step 2: Generate Reference Centers
cd ../TVMC
python ./get_reference_center.py --dataset basketball_player --num_frames 10 --num_centers 1995 --centers_dir ../arap-volume-tracking/data/basketball-output-max-2000/impr

# Step 3: Compute Transformations
python ./get_transformation.py --dataset basketball_player --num_frames 10 --num_centers 1995 --centers_dir ../arap-volume-tracking/data/basketball-output-max-2000/impr --firstIndex 11 --lastIndex 20

# Step 4: Mesh Processing
cd ../tvm-editing
dotnet new globaljson --sdk-version 5.0.408
dotnet build TVMEditor.sln --configuration Release --no-incremental
TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test basketball 1 11 20 "./TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995/" "./TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/"

# Step 5: Extract Reference Mesh
cd ../TVMC
python ./extract_reference_mesh.py --dataset basketball_player --num_frames 10 --num_centers 1995 --inputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/output/ --outputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995/reference_mesh/ --firstIndex 11 --lastIndex 20 --key 9

# Step 6: Deform the Reference Mesh to Each Mesh in the Group
cd ../tvm-editing
TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test basketball 2 11 20 "./TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995" "./TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/"

# Step 7: Compute Displacement Fields
cd ../TVMC
python ./get_displacements.py --dataset basketball_player --num_frames 10 --num_centers 1995 --target_mesh_path ../arap-volume-tracking/data/basketball_player --firstIndex 11 --lastIndex 20

# Step 8: Compression and Evaluation
python ./evaluation.py --dataset basketball_player --num_frames 10 --num_centers 1995 --firstIndex 11 --lastIndex 20 --fileNamePrefix basketball_player_fr0 --encoderPath ../draco/build/draco_encoder --decoderPath ../draco/build/draco_decoder --qp 10 --outputPath ./basketball_player_outputs

## dancer

cd ../arap-volume-tracking

dotnet new globaljson --sdk-version 7.0.410 --force

dotnet ./bin/Client.dll ./config/max/config-dancer-max.xml

#dotnet ./bin/Client.dll ./config/impr/config-dancer-impr.xml

cd ../TVMC

python ./get_reference_center.py --dataset dancer --num_frames 10 --num_centers 2000 --centers_dir ../arap-volume-tracking/data/dancer-output-max-2000/

python ./get_transformation.py --dataset dancer --num_frames 10 --num_centers 2000 --centers_dir ../arap-volume-tracking/data/dancer-output-max-2000/ --firstIndex 5 --lastIndex 14


cd ../tvm-editing
dotnet new globaljson --sdk-version 5.0.408 --force

TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test dancer 1 5 14 "./TVMEditor.Test/bin/Release/net5.0/Data/dancer_2000/" "./TVMEditor.Test/bin/Release/net5.0/output/dancer_2000/"

cd ../TVMC

python ./extract_reference_mesh.py --dataset dancer --num_frames 10 --num_centers 2000 --inputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/dancer_2000/output/ --outputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/dancer_2000/reference_mesh/ --firstIndex 5 --lastIndex 14 --key 4

cd ../tvm-editing

TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test dancer 2 5 14 "./TVMEditor.Test/bin/Release/net5.0/Data/dancer_2000" "./TVMEditor.Test/bin/Release/net5.0/output/dancer_2000/"

cd ../TVMC

python ./get_displacements.py --dataset dancer --num_frames 10 --num_centers 2000 --target_mesh_path ../arap-volume-tracking/data/dancer --firstIndex 5 --lastIndex 14

python ./evaluation.py --dataset dancer --num_frames 10 --num_centers 2000 --firstIndex 5 --lastIndex 14 --fileNamePrefix dancer_fr0 --encoderPath ../draco/build/draco_encoder --decoderPath ../draco/build/draco_decoder --qp 10 --outputPath ./dancer_outputs


## mitch

cd ../arap-volume-tracking

dotnet new globaljson --sdk-version 7.0.410 --force

dotnet ./bin/Client.dll ./config/max/config-mitch-max.xml

#dotnet ./bin/Client.dll ./config/impr/config-mitch-impr.xml

cd ../TVMC

python ./get_reference_center.py --dataset mitch --num_frames 10 --num_centers 2000 --centers_dir ../arap-volume-tracking/data/mitch-output-max-2000/

python ./get_transformation.py --dataset mitch --num_frames 10 --num_centers 2000 --centers_dir ../arap-volume-tracking/data/mitch-output-max-2000/ --firstIndex 1 --lastIndex 10


cd ../tvm-editing
dotnet new globaljson --sdk-version 5.0.408 --force

TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test mitch 1 1 10 "./TVMEditor.Test/bin/Release/net5.0/Data/mitch_2000/" "./TVMEditor.Test/bin/Release/net5.0/output/mitch_2000/"

cd ../TVMC

python ./extract_reference_mesh.py --dataset mitch --num_frames 10 --num_centers 2000 --inputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/mitch_2000/output/ --outputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/mitch_2000/reference_mesh/ --firstIndex 1 --lastIndex 10 --key 4

cd ../tvm-editing

TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test mitch 2 1 10 "./TVMEditor.Test/bin/Release/net5.0/Data/mitch_2000" "./TVMEditor.Test/bin/Release/net5.0/output/mitch_2000/"

cd ../TVMC

python ./get_displacements.py --dataset mitch --num_frames 10 --num_centers 2000 --target_mesh_path ../arap-volume-tracking/data/mitch --firstIndex 1 --lastIndex 10

python ./evaluation.py --dataset mitch --num_frames 10 --num_centers 2000 --firstIndex 1 --lastIndex 10 --fileNamePrefix mitch_fr0 --encoderPath ../draco/build/draco_encoder --decoderPath ../draco/build/draco_decoder --qp 10 --outputPath ./mitch_outputs



## thomas

cd ../arap-volume-tracking

dotnet new globaljson --sdk-version 7.0.410 --force

dotnet ./bin/Client.dll ./config/max/config-thomas-max.xml

#dotnet ./bin/Client.dll ./config/impr/config-thomas-impr.xml

cd ../TVMC

python ./get_reference_center.py --dataset thomas --num_frames 10 --num_centers 2000 --centers_dir ../arap-volume-tracking/data/thomas-output-max-2000/

python ./get_transformation.py --dataset thomas --num_frames 10 --num_centers 2000 --centers_dir ../arap-volume-tracking/data/thomas-output-max-2000/ --firstIndex 1 --lastIndex 10


cd ../tvm-editing
dotnet new globaljson --sdk-version 5.0.408 --force

TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test thomas 1 1 10 "./TVMEditor.Test/bin/Release/net5.0/Data/thomas_2000/" "./TVMEditor.Test/bin/Release/net5.0/output/thomas_2000/"

cd ../TVMC

python ./extract_reference_mesh.py --dataset thomas --num_frames 10 --num_centers 2000 --inputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/thomas_2000/output/ --outputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/thomas_2000/reference_mesh/ --firstIndex 1 --lastIndex 10 --key 4

cd ../tvm-editing

TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test thomas 2 1 10 "./TVMEditor.Test/bin/Release/net5.0/Data/thomas_2000" "./TVMEditor.Test/bin/Release/net5.0/output/thomas_2000/"

cd ../TVMC

python ./get_displacements.py --dataset thomas --num_frames 10 --num_centers 2000 --target_mesh_path ../arap-volume-tracking/data/thomas --firstIndex 1 --lastIndex 10

python ./evaluation.py --dataset thomas --num_frames 10 --num_centers 2000 --firstIndex 1 --lastIndex 10 --fileNamePrefix thomas_fr0 --encoderPath ../draco/build/draco_encoder --decoderPath ../draco/build/draco_decoder --qp 10 --outputPath ./thomas_outputs


## generate figures

python ./objective_results_all.py