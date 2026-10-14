FROM mcr.microsoft.com/dotnet/sdk:10.0-noble

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PYTHONUNBUFFERED=1 \
    TVMC_CONTAINER=1 \
    DRACO_ENCODER=/opt/draco/build/draco_encoder \
    DRACO_DECODER=/opt/draco/build/draco_decoder

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git libgl1 libglib2.0-0 libgomp1 python3 python3-pip python3-venv \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/google/draco.git /opt/draco \
    && cmake -S /opt/draco -B /opt/draco/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /opt/draco/build --parallel

COPY requirements.txt /tmp/tvmc-requirements.txt
RUN python3 -m pip install --no-cache-dir -r /tmp/tvmc-requirements.txt

# Cache NuGet packages in the image. The mounted workspace is built quickly at runtime.
COPY arap-volume-tracking /tmp/tvmc-restore/arap-volume-tracking
COPY tvm-editing /tmp/tvmc-restore/tvm-editing
RUN dotnet restore /tmp/tvmc-restore/arap-volume-tracking/Client/Client.csproj -p:NuGetAudit=false \
    && dotnet restore /tmp/tvmc-restore/tvm-editing/TVMEditor.Test/TVMEditor.Test.csproj -p:NuGetAudit=false \
    && rm -rf /tmp/tvmc-restore

WORKDIR /workspace
ENTRYPOINT ["/bin/bash", "/workspace/docker-entrypoint.sh"]
CMD ["basketball"]
