#include "send_image.hpp"

#include <arpa/inet.h>
#include <fmt/core.h>
#include <cstdlib>
#include <cstdint>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdexcept>

#include <cstring>
#include <iostream>

SenderToPython::SenderToPython(const char* ip, uint16_t port)
{
  this->fd = socket(AF_INET, SOCK_STREAM, 0);

  if (this->fd == -1)
  {
    throwError("socket creation failed");
  }

  this->server_addr = parseSockAddr(ip, port);

  if (connect(this->fd, (sockaddr*)&this->server_addr, sizeof(this->server_addr)) == -1)
  {
    throwError("connect failed");
  }
}

int SenderToPython::get_socket()
{
  return this->fd;
}

sockaddr_in SenderToPython::parseSockAddr(const char* ip, uint16_t port)
{
  sockaddr_in addr;
  // memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  std::cout << "ip: " << ip << std::endl;
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
  {
    throwError("inet_pton error");
  }

  return addr;
}

void SenderToPython::printError(const char* prefix)
{
  const char* msg = strerror(errno);
  std::cerr << fmt::format("{}: {}\n", prefix, msg);
}

void SenderToPython::throwError(const char* prefix)
{
  const char* msg = strerror(errno);
  throw std::runtime_error(fmt::format("{}: {}", prefix, msg));
}
