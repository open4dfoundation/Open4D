# TVMC Quick Start — Ubuntu Linux

The following instructions are for Ubuntu 22.04.

## 1. Install system dependencies

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git wget \
  python3 python3-venv \
  libgl1 libglib2.0-0
```

Install the .NET 10 SDK:

```bash
wget https://dot.net/v1/dotnet-install.sh
bash dotnet-install.sh --channel 10.0
export DOTNET_ROOT="$HOME/.dotnet"
export PATH="$DOTNET_ROOT:$PATH"
```

Add the two `export` lines to `~/.bashrc` if you want them to persist in new terminals.

## 2. Enter the TVMC directory

From the Open4D repository root:

```bash
cd modules/tvmc
```

## 3. Set up TVMC

This creates the Python environment and builds the .NET projects and Draco:

```bash
./setup.sh
```

## 4. Run the complete pipeline

```bash
./run_pipeline.sh basketball
```

The first run takes several minutes. If it stops, resume from a stage without repeating earlier work:

```bash
./run_pipeline.sh basketball --from reference-centers
```

## Results

Reconstructed meshes and evaluation metrics are written to:

```text
TVMC/basketball_player_outputs/
```

The summary is saved as:

```text
TVMC/basketball_player_outputs/metrics.json
```
