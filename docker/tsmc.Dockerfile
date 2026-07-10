FROM mcr.microsoft.com/dotnet/sdk:7.0
COPY --from=mcr.microsoft.com/dotnet/runtime:5.0 /usr/share/dotnet/shared
/usr/share/dotnet/shared
RUN apt-get update && apt-get install -y python3 python3-pip cmake build-essential
COPY modules/tsmc /opt/tsmc
WORKDIR /opt/tsmc
conda env create -f environment.yml
ENTRYPOINT ["./run.sh"]
