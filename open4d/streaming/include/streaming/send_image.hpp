#pragma once

#include <cstdint>
#include <cstdlib>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

class SenderToPython
{
  public:
    SenderToPython(const char* ip, uint16_t port);
    ~SenderToPython()
    {
      close(this->fd);
    };

    int get_socket();
    sockaddr_in parseSockAddr(const char* ip, uint16_t port);
    void throwError(const char* prefix);
    void printError(const char* prefix);

  private:
    int fd;
    sockaddr_in server_addr;
};
