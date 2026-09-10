#pragma once

#include <functional>
#include <thread>
#include <utility>

class CaptureThread
{
public:
  CaptureThread(std::function<void()> work, std::function<void()> stop)
    : stop_(std::move(stop)), thread_(std::move(work))
  {
  }

  CaptureThread(const CaptureThread&) = delete;
  CaptureThread& operator=(const CaptureThread&) = delete;

  ~CaptureThread()
  {
    if (thread_.joinable())
    {
      stop_();
      thread_.join();
    }
  }

  void join()
  {
    if (thread_.joinable())
      thread_.join();
  }

private:
  std::function<void()> stop_;
  std::thread thread_;
};
