# TVMC Quick Start

TVMC supports Homebrew macOS and Ubuntu Linux. It uses Open4D's shared Python
3.12 codec environment and additionally requires CMake, Git, and the .NET 10
SDK.

## 1. Install system dependencies

### Homebrew macOS

Install [Homebrew](https://brew.sh/) if it is not already available, then run:

```bash
brew update
brew install cmake python@3.12 dotnet git
```

Verify the required tools:

```bash
python3.12 --version
dotnet --version
cmake --version
```

### Ubuntu Linux

On Ubuntu 24.04, install the system packages:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git curl \
  python3 python3-venv \
  libgl1 libglib2.0-0
```

Install the .NET 10 SDK for your user account:

```bash
curl -sSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
bash /tmp/dotnet-install.sh --channel 10.0

export DOTNET_ROOT="$HOME/.dotnet"
export PATH="$DOTNET_ROOT:$PATH"
```

Add the two `export` lines to `~/.bashrc` to make them available in future
shells, then verify the required tools:

```bash
python3 --version
dotnet --version
cmake --version
```

## 2. Create the shared Python environment

From the Open4D repository root:

```bash
conda env create -f environment.yml
conda activate open4d
python -m pip install -e .
```

This is the same Python 3.12 dependency set used by the other codecs. TVMC's
local `requirements.txt` mirrors the relevant pins but is not a second supported
dependency baseline.

## 3. Enter the TVMC directory

From the Open4D repository root:

```bash
cd open4d/codecs/tvmc
```

The ten basketball sample meshes are included in the repository.

## 4. Build TVMC's native dependencies

Run the setup script once:

```bash
./setup.sh --build-only
```

This builds the .NET projects, initializes Draco, and builds the Draco encoder
and decoder. Running `./setup.sh` without `--build-only` remains a convenience
for creating a component-local `.venv`, but it uses the same Python 3.12 pins.

## 5. Run the pipeline

```bash
./run_pipeline.sh basketball
```

The first run takes several minutes. To inspect the commands without running them:

```bash
./run_pipeline.sh basketball --dry-run
```

If a later stage fails, resume without repeating volume tracking:

```bash
./run_pipeline.sh basketball --from reference-centers
```

## CPU performance

Python stages use parallel `cKDTree` queries, vectorized deformation and distance
calculations, and cached subdivision, vertex mappings and fitted outputs.
.NET tracking and external Draco processes are unaffected.

To time the optimized evaluation stage after the earlier stages have produced
their inputs:

```bash
time ./run_pipeline.sh basketball --only evaluation
```

Record the CPU model, core count, TVMC revision, configuration, frame range,
and whether intermediate files were already present when reporting results.

## Results

Reconstructed meshes are written to:

```text
TVMC/basketball_player_outputs/
```

Evaluation metrics are saved in:

```text
TVMC/basketball_player_outputs/metrics.json
```

## View the outputs

TVMC produces a sequence of decoded OBJ meshes. From `open4d/codecs/tvmc`, use
the public Open4D sequence viewer:

```bash
python -m pip install -e '../../..[player]'
python ../../../examples/visualization/visualize_sequence.py \
  TVMC/basketball_player_outputs/ --fps 10
```

### Outputs generated on an SSH machine

Run the following command on your **local machine**, replacing the SSH host and
remote repository path:

```bash
rsync -avP \
  USER@SSH_HOST:/path/to/Open4D/open4d/codecs/tvmc/TVMC/basketball_player_outputs/ \
  /path/to/local/Open4D/open4d/codecs/tvmc/TVMC/basketball_player_outputs/
```

Then enter the local TVMC directory and launch the player:

```bash
cd /path/to/local/Open4D/open4d/codecs/tvmc

python -m pip install -e '../../..[player]'
python ../../../examples/visualization/visualize_sequence.py \
  TVMC/basketball_player_outputs/ --fps 10
```
