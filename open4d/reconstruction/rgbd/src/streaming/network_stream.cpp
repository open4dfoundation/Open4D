#include "network_stream.hpp"

#include <arpa/inet.h>
#include <fmt/core.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

static void printError(const char* prefix)
{
  const char* msg = strerror(errno);
  std::cerr << fmt::format("{}: {}\n", prefix, msg);
}

void throwError(const char* prefix)
{
  const char* msg = strerror(errno);
  throw std::runtime_error(fmt::format("{}: {}", prefix, msg));
}

FileDesc::FileDesc() : fd{-1}
{
}

FileDesc::FileDesc(int fd_) : fd{fd_}
{
}

FileDesc::FileDesc(FileDesc&& v) : fd{v.fd}
{
  v.fd = -1;
}

FileDesc::~FileDesc()
{
  if (fd >= 0)
  {
    if (close(fd) < 0)
      printError("close");
  }
}

static sockaddr_in parseSockAddr(const char* ip, uint16_t port)
{
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
    throwError("inet_pton");
  return addr;
}

NetworkStream::NetworkStream(const char* ip, uint16_t port)
{
  auto addr = parseSockAddr(ip, port);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    throwError("socket");
  while (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0)
  {
    printError("connect");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  }
}

void NetworkStream::sendAll(const void* buf, size_t n)
{
  while (n > 0)
  {
    ssize_t r = send(fd, buf, n, MSG_NOSIGNAL);
    if (__builtin_expect(r <= 0, 0))
    {
      throwError("send");
    }
    buf = static_cast<const char*>(buf) + r;
    n -= r;
  }
}

NetworkListener::NetworkListener(const char* ip, uint16_t port)
{
  auto addr = parseSockAddr(ip, port);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    throwError("socket");
  int opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))
      < 0)
    throwError("setsockopt");
  if (bind(fd, (const sockaddr*)(&addr), sizeof(addr)) < 0)
    throwError("bind");
  if (listen(fd, 4) < 0)
    throwError("listen");
  std::cerr << fmt::format("Listening: {}:{}\n", ip, port);
}

NetworkStream NetworkListener::accept()
{
  sockaddr_in addrConn;
  socklen_t addrConnSize = sizeof(addrConn);
  int fdConn = ::accept(fd, (sockaddr*)&addrConn, &addrConnSize);
  if (fdConn < 0)
    throwError("accept");
  char buf[INET_ADDRSTRLEN];
  if (!inet_ntop(addrConn.sin_family, &addrConn.sin_addr, buf, addrConnSize))
    printError("Accepted");
  else
    std::cerr << fmt::format("Accepted: {}\n", buf);
  return NetworkStream{fdConn};
}
