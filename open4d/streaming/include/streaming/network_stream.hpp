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
  FileDesc(FileDesc&& v) noexcept;
  ~FileDesc();
  FileDesc& operator=(FileDesc&& v) noexcept;
};

// Timeouts are milliseconds (default 30000) per connect, accept, or complete send.
class NetworkStream : public FileDesc
{
  friend class NetworkListener;
  NetworkStream(int fd_, int timeout_ms);
  int timeout_ms_;

public:
  NetworkStream(const char* ip, uint16_t port, int timeout_ms = 30000);
  void sendAll(const void* buf, size_t n);
};

class NetworkListener : public FileDesc
{
  int timeout_ms_;

public:
  // Accepted streams inherit this timeout for sends.
  NetworkListener(const char* ip, uint16_t port, int timeout_ms = 30000);
  NetworkStream accept();
};
