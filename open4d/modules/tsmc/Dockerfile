# TSMC pipeline image.
#
# Bundles everything the *deterministic* compression pipeline (README steps
# 2-10 / run.sh) needs: the Python env (environment.yml), .NET SDK 7.0 (ARAP)
# and 5.0 (TVMEditor), Google Draco, and pre-built .NET projects.
#
# SAM3 (step 1) is NOT baked in: it needs large pretrained checkpoints and a
# GPU at run time. Install it inside a running container with ./install_sam3.sh
# and mount/download the checkpoints (see README).
#
# Build (context = this directory):
#   docker build -t tsmc .
# Run (GPU + your data mounted in):
#   docker run --gpus all -it --rm -v "$PWD/data:/workspace/tsmc/data" tsmc
#   # inside the container:
#   ./run.sh
#
# CUDA 12.6 + cuDNN matches torch 2.7.0+cu126 in environment.yml. The -devel
# base ships nvcc, which SAM3 needs if you build its CUDA extensions later.
FROM nvidia/cuda:12.6.2-cudnn-devel-ubuntu24.04

ENV DEBIAN_FRONTEND=noninteractive \
    DOTNET_ROOT=/opt/dotnet \
    CONDA_DIR=/opt/conda
ENV PATH=$CONDA_DIR/envs/tsmc/bin:$CONDA_DIR/bin:$DOTNET_ROOT:$PATH \
    DOTNET_CLI_TELEMETRY_OPTOUT=1 \
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1

# --- System dependencies -----------------------------------------------------
# build-essential + cmake  -> Draco
# libicu-dev               -> .NET runtime
# libgl1/libgomp1/libx*    -> Open3D
RUN apt-get update && apt-get install -y --no-install-recommends \
        git wget curl ca-certificates build-essential cmake \
        libicu-dev libgl1 libgomp1 libglib2.0-0 libx11-6 libxext6 libxrender1 \
    && rm -rf /var/lib/apt/lists/*

# --- Miniconda ---------------------------------------------------------------
RUN wget -q https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh -O /tmp/miniconda.sh \
    && bash /tmp/miniconda.sh -b -p "$CONDA_DIR" \
    && rm /tmp/miniconda.sh

WORKDIR /workspace/tsmc

# --- Python environment (cache-friendly: only env spec first) ----------------
COPY environment.yml ./
RUN conda env create -f environment.yml && conda clean -afy

# --- Native toolchain + source ----------------------------------------------
COPY . .
# .NET SDK 7.0 (ARAP) and 5.0 (TVMEditor)
RUN bash setup.sh dotnet-sdk
# Google Draco + build the two .NET projects
RUN bash setup.sh all

# The env's python is already first on PATH, so run.sh picks it up via PYTHON=python.
CMD ["bash"]
