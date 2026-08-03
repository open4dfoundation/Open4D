#pragma once
#include <stdio.h>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <set>

#define PROFC_NODE(name)                              \
  static ProfileNode __node##__LINE__(name);          \
  TheNodeList::Instance().AddNode(&__node##__LINE__); \
  ScopedTimer __timer##__LINE__(std::bind(            \
      &ProfileNode::Accumulate, &__node##__LINE__, std::placeholders::_1));

class ProfileNode {
 public:
  explicit ProfileNode(const std::string& name) : name_(name), count_(0) {
  }
  void Accumulate(std::chrono::microseconds us) {
    the_lock_.lock();
    count_++;
    elapsed_us_ += us;
    the_lock_.unlock();
  }
  void Print() {
    printf(
        "%-25s %10d %10dms %10dus\n", name_.c_str(), count_,
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                             elapsed_us_).count()),
        static_cast<int>(elapsed_us_.count() / count_));
  }

 private:
  std::string name_;
  int count_;
  std::chrono::microseconds elapsed_us_;
  std::mutex the_lock_;
};

class ScopedTimer {
 public:
  explicit ScopedTimer(std::function<void(std::chrono::microseconds)> callback)
      : callback_(callback) {
    start_ = std::chrono::system_clock::now();
  }
  ~ScopedTimer() {
    auto end = std::chrono::system_clock::now();
    auto elapsed = end - start_;
    callback_(std::chrono::duration_cast<std::chrono::microseconds>(elapsed));
  }
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  std::function<void(std::chrono::microseconds)> callback_;
  std::chrono::time_point<std::chrono::system_clock> start_;
};

class TheNodeList {
 public:
  void AddNode(ProfileNode* node) {
    nodes_.insert(node);
  }
  ~TheNodeList() {
    Print();
  }
  static TheNodeList& Instance() {
    static TheNodeList nodes;
    return nodes;
  }
  void Print() {
    printf("--------------------------------------------------------------\n");
    printf("name                           count      elapsed      us/call\n");
    for (auto node : nodes_) node->Print();
  }

 private:
  std::set<ProfileNode*> nodes_;
};