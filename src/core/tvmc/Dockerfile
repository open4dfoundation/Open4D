FROM ubuntu:20.04

# Set noninteractive mode for apt-get
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends apt-transport-https ca-certificates

# Install dependencies
RUN apt-get update && apt-get install -y \
    wget \
    git \
    cmake \
    g++ \
    build-essential \
    python3 \
    python3-pip \
    python3-venv \
    sudo \
    curl \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Install OpenGL dependencies for Open3D
RUN apt-get update && apt-get install -y \
    libgl1-mesa-glx \
    libglib2.0-0

# Install .NET 7.0 SDK and runtime
RUN wget https://packages.microsoft.com/config/ubuntu/20.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb \
    && dpkg -i packages-microsoft-prod.deb \
    && rm packages-microsoft-prod.deb \
    && apt-get update \
    && apt-get install -y dotnet-sdk-7.0 aspnetcore-runtime-7.0 \
    && apt-get install -y dotnet-sdk-5.0 aspnetcore-runtime-5.0

# Install Miniconda
RUN wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh -O miniconda.sh \
    && bash miniconda.sh -b -p /opt/conda \
    && rm miniconda.sh \
    && /opt/conda/bin/conda init

ENV PATH="/opt/conda/bin:$PATH"

# Create and activate conda environment
RUN conda create -n open3d_env python=3.8 numpy open3d=0.18.0 scikit-learn scipy matplotlib trimesh=4.1.0 -c conda-forge

# Clone the project
WORKDIR /app
RUN git clone https://github.com/SINRG-Lab/TVMC.git

WORKDIR /app/TVMC
RUN git clone https://github.com/google/draco.git && \
    cd draco && mkdir build && cd build && \
    cmake ../ && make -j$(nproc)

# Install additional Python dependencies
RUN /opt/conda/envs/open3d_env/bin/pip install --upgrade pip && \
    /opt/conda/envs/open3d_env/bin/pip install numpy scikit-learn scipy trimesh

# Set environment variables
ENV PATH="/opt/conda/envs/open3d_env/bin:$PATH"
ENV CONDA_DEFAULT_ENV=open3d_env

# Set working directory
WORKDIR /app/TVMC

CMD ["/bin/bash"]
