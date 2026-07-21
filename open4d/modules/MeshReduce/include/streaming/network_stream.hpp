#pragma once

#include <cstdint>
#include <cstdlib>

class FileDesc
{
  FileDesc(const FileDesc&) = delete;
  FileDesc& operator=(const FileDesc&) = delete;

protected:
  int fd;

  FileDesc();
  FileDesc(int fd_);
  FileDesc(FileDesc&& v);
  ~FileDesc();
  FileDesc& operator=(FileDesc&& v);
};

class NetworkStream : public FileDesc
{
  using FileDesc::FileDesc;
  friend class NetworkListener;

public:
  NetworkStream(const char* ip, uint16_t port);
  void sendAll(const void* buf, size_t n);
};

class NetworkListener : public FileDesc
{
public:
  NetworkListener(const char* ip, uint16_t port);
  NetworkStream accept();
};
