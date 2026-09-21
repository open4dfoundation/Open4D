#include "network_stream.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

static void printError(const char* prefix)
{
  const char* msg = strerror(errno);
  std::cerr << prefix << ": " << msg << '\n';
}

void throwError(const char* prefix)
{
  const char* msg = strerror(errno);
  throw std::runtime_error(std::string(prefix) + ": " + msg);
}

FileDesc::FileDesc() : fd{-1}
{
}

FileDesc::FileDesc(int fd_) : fd{fd_}
{
}

FileDesc::FileDesc(FileDesc&& v) noexcept : fd{v.fd}
{
  v.fd = -1;
}

FileDesc& FileDesc::operator=(FileDesc&& v) noexcept
{
  if (this != &v)
  {
    if (fd >= 0)
      close(fd);
    fd = v.fd;
    v.fd = -1;
  }
  return *this;
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
    throw std::invalid_argument("invalid IPv4 address");
  return addr;
}

using Clock = std::chrono::steady_clock;

static int checkTimeout(int timeout_ms)
{
  if (timeout_ms <= 0)
    throw std::invalid_argument("socket timeout_ms must be positive");
  return timeout_ms;
}

static void setNonblocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    throwError("fcntl");
}

static void checkDeadline(Clock::time_point deadline)
{
  if (Clock::now() >= deadline)
    throw std::runtime_error("socket operation timed out");
}

static void waitFor(int fd, short events, Clock::time_point deadline)
{
  while (true)
  {
    checkDeadline(deadline);
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
        deadline - Clock::now()).count();
    if (remaining <= 0)
      throw std::runtime_error("socket operation timed out");
    pollfd descriptor{fd, events, 0};
    const int result = poll(&descriptor, 1, static_cast<int>(remaining));
    if (result > 0)
    {
      if (descriptor.revents & POLLNVAL)
        throw std::runtime_error("socket descriptor is closed");
      return;
    }
    if (result < 0 && errno != EINTR)
      throwError("poll");
  }
}

NetworkStream::NetworkStream(const char* ip, uint16_t port, int timeout_ms)
  : timeout_ms_(checkTimeout(timeout_ms))
{
  auto addr = parseSockAddr(ip, port);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    throwError("socket");
  setNonblocking(fd);
  if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0)
  {
    if (errno != EINPROGRESS && errno != EINTR && errno != EWOULDBLOCK)
      throwError("connect");
    waitFor(fd, POLLOUT, Clock::now() + std::chrono::milliseconds(timeout_ms_));
    int error = 0;
    socklen_t size = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0)
      throwError("getsockopt");
    if (error != 0)
    {
      errno = error;
      throwError("connect");
    }
  }
}

NetworkStream::NetworkStream(int fd_, int timeout_ms)
  : FileDesc(fd_), timeout_ms_(timeout_ms)
{
  setNonblocking(fd);
}

void NetworkStream::sendAll(const void* buf, size_t n)
{
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
  while (n > 0)
  {
    checkDeadline(deadline);
    ssize_t r = send(fd, buf, n, MSG_NOSIGNAL);
    if (r < 0)
    {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        waitFor(fd, POLLOUT, deadline);
        continue;
      }
      throwError("send");
    }
    if (r == 0)
      throw std::runtime_error("socket closed during send");
    buf = static_cast<const char*>(buf) + r;
    n -= r;
  }
}

NetworkListener::NetworkListener(const char* ip, uint16_t port, int timeout_ms)
  : timeout_ms_(checkTimeout(timeout_ms))
{
  auto addr = parseSockAddr(ip, port);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    throwError("socket");
  int opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))
      < 0)
    throwError("setsockopt");
  if (bind(fd, (const sockaddr*)(&addr), sizeof(addr)) < 0)
    throwError("bind");
  if (listen(fd, 4) < 0)
    throwError("listen");
  setNonblocking(fd);
  std::cerr << "Listening: " << ip << ':' << port << '\n';
}

NetworkStream NetworkListener::accept()
{
  sockaddr_in addrConn;
  socklen_t addrConnSize = sizeof(addrConn);
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
  int fdConn;
  while (true)
  {
    checkDeadline(deadline);
    fdConn = ::accept(fd, (sockaddr*)&addrConn, &addrConnSize);
    if (fdConn >= 0)
      break;
    if (errno == EINTR)
      continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      throwError("accept");
    waitFor(fd, POLLIN, deadline);
  }
  NetworkStream connection(fdConn, timeout_ms_);
  char buf[INET_ADDRSTRLEN];
  if (!inet_ntop(addrConn.sin_family, &addrConn.sin_addr, buf, sizeof(buf)))
    printError("Accepted");
  else
    std::cerr << "Accepted: " << buf << '\n';
  return connection;
}
