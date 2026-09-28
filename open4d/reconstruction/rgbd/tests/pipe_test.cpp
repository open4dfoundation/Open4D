#include "pipe.hpp"
#include "capture_thread.hpp"

#include <cassert>
#include <future>
#include <atomic>
#include <string>

template <typename Pipe>
void rejects_after_termination(Pipe& pipe)
{
  pipe.terminate();
  try
  {
    pipe.fetch();
    assert(false);
  }
  catch (const InTerminatedException&) {}
  try
  {
    pipe.put("late");
    assert(false);
  }
  catch (const InTerminatedException&) {}
}

int main()
{
  PipeDataIn<std::string> empty;
  rejects_after_termination(empty);
  PipeDataInOnce<std::string> once;
  rejects_after_termination(once);

  PipeDataIn<std::string> pending;
  pending.put("last");
  pending.terminate();
  assert(pending.fetch() == "last");
  rejects_after_termination(pending);

  PipeDataIn<std::string> blocked;
  auto consumer = std::async(std::launch::async, [&] {
    try { blocked.fetch(); }
    catch (const InTerminatedException&) { return true; }
    return false;
  });
  blocked.terminate();
  assert(consumer.get());

  PipeDataIn<std::string> full;
  full.put("first");
  auto producer = std::async(std::launch::async, [&] {
    try { full.put("second"); }
    catch (const InTerminatedException&) { return true; }
    return false;
  });
  full.terminate();
  assert(producer.get());
  assert(full.fetch() == "first");

  std::atomic<bool> exited{false};
  try
  {
    PipeDataIn<std::string> queue;
    CaptureThread capture([&] {
      try { queue.fetch(); }
      catch (const InTerminatedException&) {}
      exited = true;
    }, [&] { queue.terminate(); });
    throw std::runtime_error("reconstruction failed");
  }
  catch (const std::runtime_error&) {}
  assert(exited);
}
