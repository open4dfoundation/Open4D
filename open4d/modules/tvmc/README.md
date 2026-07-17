# TVMC Quick Start

TVMC supports Homebrew macOS and Ubuntu Linux. It requires Python 3.8–3.11,
CMake, Git, and the .NET 10 SDK.

## 1. Install system dependencies

### Homebrew macOS

Install [Homebrew](https://brew.sh/) if it is not already available, then run:

```bash
brew update
brew install cmake python@3.11 dotnet git
```

Verify the required tools:

```bash
python3.11 --version
dotnet --version
cmake --version
```

### Ubuntu Linux

Ubuntu 22.04 includes Python 3.10, which is supported by TVMC. Install the
system packages:

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

## 2. Enter the TVMC directory

From the Open4D repository root:

```bash
cd open4d/modules/tvmc
```

The ten basketball sample meshes are included in the repository.

## 3. Set up TVMC

Run the setup script once:

```bash
./setup.sh
```

This creates `.venv`, installs the Python packages, builds the .NET projects, initializes Draco, and builds the Draco encoder and decoder.

## 4. Run the pipeline

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

TVMC produces a sequence of decoded OBJ meshes. From `open4d/modules/tvmc`, use the
Open4D OBJ-sequence player to view them:

```bash
.venv/bin/python ../tsmc/tsmc/player.py \
  --mesh-dir TVMC/basketball_player_outputs \
  --pattern 'decoded_*.obj' \
  --fps 10 \
  --loop
```

### Outputs generated on an SSH machine

Run the following command on your **local machine**, replacing the SSH host and
remote repository path:

```bash
rsync -avP \
  USER@SSH_HOST:/path/to/Open4D/open4d/modules/tvmc/TVMC/basketball_player_outputs/ \
  /path/to/local/Open4D/open4d/modules/tvmc/TVMC/basketball_player_outputs/
```

Then enter the local TVMC directory and launch the player:

```bash
cd /path/to/local/Open4D/open4d/modules/tvmc

.venv/bin/python ../tsmc/tsmc/player.py \
  --mesh-dir TVMC/basketball_player_outputs \
  --pattern 'decoded_*.obj' \
  --fps 10 \
  --loop
```

Run `./setup.sh` in the local TVMC directory first if `.venv` does not exist.
