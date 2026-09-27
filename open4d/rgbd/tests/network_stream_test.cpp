#include "network_stream.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

class Descriptor : public FileDesc
{
public:
  explicit Descriptor(int value) : FileDesc(value) {}
  Descriptor(Descriptor&&) = default;
  Descriptor& operator=(Descriptor&&) = default;
  int get() const { return fd; }
};

class Listener : public NetworkListener
{
public:
  using NetworkListener::NetworkListener;
  int get() const { return fd; }
  uint16_t port() const
  {
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    assert(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    return ntohs(address.sin_port);
  }
};

class Stream : public NetworkStream
{
public:
  explicit Stream(NetworkStream&& stream) : NetworkStream(std::move(stream)) {}
  int get() const { return fd; }
};

static sockaddr_in endpoint(uint16_t port)
{
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  return address;
}

static Descriptor connect_client(uint16_t port)
{
  Descriptor client(socket(AF_INET, SOCK_STREAM, 0));
  auto address = endpoint(port);
  assert(connect(client.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  timeval timeout{2, 0};
  assert(setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
  return client;
}

template <typename Operation>
static void expect_error(Operation operation, const char* message, const char* name = nullptr)
{
  const auto start = std::chrono::steady_clock::now();
  const auto cpu_start = std::clock();
  bool failed = false;
  try { operation(); }
  catch (const std::exception& error)
  {
    failed = true;
    if (std::string(error.what()).find(message) == std::string::npos)
      std::cerr << "Expected " << message << ", got " << error.what() << '\n';
    assert(std::string(error.what()).find(message) != std::string::npos);
  }
  assert(failed);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  if (elapsed >= 2s || std::getenv("OPEN4D_SOCKET_TIMINGS"))
    std::cerr << (name ? name : message) << ": wall_ms="
              << std::chrono::duration<double, std::milli>(elapsed).count()
              << " cpu_ms=" << 1000.0 * (std::clock() - cpu_start) / CLOCKS_PER_SEC << '\n';
}

static volatile sig_atomic_t interruptions = 0;
static void interrupt(int) { ++interruptions; }

int main()
{
  expect_error([] { NetworkStream stream("bad-address", 1); }, "IPv4");
  expect_error([] { NetworkListener listener("127.0.0.1", 0, 0); }, "timeout");

  {
    Descriptor first(socket(AF_INET, SOCK_STREAM, 0));
    Descriptor second(socket(AF_INET, SOCK_STREAM, 0));
    const int discarded = second.get();
    const int retained = first.get();
    second = std::move(first);
    assert(first.get() == -1 && second.get() == retained);
    assert(fcntl(discarded, F_GETFD) == -1);
  }

  {
    Descriptor reserved(socket(AF_INET, SOCK_STREAM, 0));
    auto address = endpoint(0);
    assert(bind(reserved.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    assert(getsockname(reserved.get(), reinterpret_cast<sockaddr*>(&address), &size) == 0);
    expect_error([&] { NetworkStream stream("127.0.0.1", ntohs(address.sin_port), 50); }, "", "connect timeout_ms=50");
    reserved = Descriptor(-1);
    for (int attempt = 0; attempt < 3; ++attempt)
      expect_error([&] { NetworkStream stream("127.0.0.1", ntohs(address.sin_port), 50); }, "connect");
  }

  Listener listener("127.0.0.1", 0);
  int reuse = 0;
  socklen_t option_size = sizeof(reuse);
  assert(getsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, &option_size) == 0);
  assert(reuse != 0);
  int available = socket(AF_INET, SOCK_STREAM, 0);
  close(available);
  expect_error([&] { Listener conflict("127.0.0.1", listener.port()); }, "bind");
  Descriptor next(socket(AF_INET, SOCK_STREAM, 0));
  assert(next.get() == available);

  {
    Listener idle("127.0.0.1", 0, 30);
    expect_error([&] { idle.accept(); }, "timed out", "accept timeout_ms=30");
  }

  struct sigaction action{};
  action.sa_handler = interrupt;
  sigemptyset(&action.sa_mask);
  assert(sigaction(SIGUSR1, &action, nullptr) == 0);
  const pthread_t waiting_thread = pthread_self();
  std::thread connector([&] {
    std::this_thread::sleep_for(20ms);
    pthread_kill(waiting_thread, SIGUSR1);
    std::this_thread::sleep_for(20ms);
    auto client = connect_client(listener.port());
  });
  auto accepted = listener.accept();
  connector.join();
  assert(interruptions == 1);

  const std::vector<char> payload(2 * 1024 * 1024, 'x');
  {
    auto client = connect_client(listener.port());
    Stream sender(listener.accept());
    const int buffer_size = 4096;
    assert(setsockopt(sender.get(), SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) == 0);
    std::thread reader([&] {
      std::this_thread::sleep_for(20ms);
      pthread_kill(waiting_thread, SIGUSR1);
      std::this_thread::sleep_for(20ms);
      std::vector<char> received(payload.size());
      size_t offset = 0;
      while (offset < received.size())
      {
        const auto count = recv(client.get(), received.data() + offset,
                                std::min(size_t(8191), received.size() - offset), 0);
        assert(count > 0);
        offset += count;
      }
      assert(received == payload);
    });
    sender.sendAll(payload.data(), payload.size());
    reader.join();
    assert(interruptions == 2);
  }

  {
    Listener idle("127.0.0.1", 0, 50);
    auto client = connect_client(idle.port());
    Stream sender(idle.accept());
    assert(fcntl(sender.get(), F_GETFL) & O_NONBLOCK);
    const int buffer_size = 4096;
    assert(setsockopt(sender.get(), SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) == 0);
    expect_error([&] { sender.sendAll(payload.data(), payload.size()); }, "timed out", "send timeout_ms=50");
  }

  {
    auto client = connect_client(listener.port());
    Stream sender(listener.accept());
    linger reset{1, 0};
    assert(setsockopt(client.get(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0);
    client = Descriptor(-1);
    pollfd descriptor{sender.get(), POLLIN, 0};
    assert(poll(&descriptor, 1, 1000) > 0);
    expect_error([&] { sender.sendAll(payload.data(), payload.size()); }, "send");
  }
}
