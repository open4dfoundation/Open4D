#pragma once

#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <vector>

class InTerminatedException : public std::exception
{
};

template <typename TIn>
class PipeData
{
public:
  virtual void put(const TIn& data) = 0;
  virtual TIn fetch() = 0;
  virtual void terminate() = 0;
};

template <typename TIn>
class PipeDataIn : public PipeData<TIn>
{
public:
  void put(const TIn& data) override
  {
    std::unique_lock<std::mutex> lock(mtx);

    while (isDataPresent)
    {
      if (isTerminated)
      {
        throw InTerminatedException();
      }
      cvFetchData.wait(lock);
    }

    readyInData = std::move(data);

    isDataPresent = true;
    cvPutData.notify_one();
  }

  TIn fetch() override
  {
    std::unique_lock<std::mutex> lock(mtx);

    while (!isDataPresent)
    {
      if (isTerminated)
      {
        throw InTerminatedException();
      }
      cvPutData.wait(lock);
    }

    TIn outData = std::move(readyInData);

    isDataPresent = false;
    cvFetchData.notify_one();

    return outData;
  }

  void terminate() override
  {
    std::unique_lock<std::mutex> lock(mtx);

    while (isDataPresent && !isTerminated)
    {
      cvFetchData.wait(lock);
    }

    isTerminated = true;
    isDataPresent = true;
    cvPutData.notify_all();
    cvFetchData.notify_all();
  }

private:
  TIn readyInData;
  bool isDataPresent = false;
  std::mutex mtx;
  std::condition_variable cvPutData, cvFetchData;
  bool isTerminated = false;
};

template <typename TIn>
class PipeDataInOnce : public PipeDataIn<TIn>
{
public:
  void put(const TIn& data) override
  {
    std::unique_lock<std::mutex> lock(mtx);

    readyInData = std::move(data);

    isDataPresent = true;
    cvPutData.notify_one();
  }

  TIn fetch() override
  {
    std::unique_lock<std::mutex> lock(mtx);

    if (!isDataPresent)
    {
      if (isTerminated)
      {
        throw InTerminatedException();
      }
      cvPutData.wait(lock);
    }
    if (isTerminated)
    {
      throw InTerminatedException();
    }

    return this->readyInData;
  }

  void terminate() override
  {
    std::unique_lock<std::mutex> lock(mtx);

    isTerminated = true;
    isDataPresent = true;
    cvPutData.notify_all();
  }

private:
  TIn readyInData;
  bool isDataPresent = false;
  std::mutex mtx;
  std::condition_variable cvPutData;
  bool isTerminated = false;
};

template <typename TIn>
class PipeDataInCollection
{
public:
  PipeDataInCollection(size_t nCams)
  {
    for (size_t i = 0; i < nCams; ++i)
    {
      pipes_.push_back(std::make_unique<PipeDataIn<TIn>>());
    }
  }

  void put(size_t idx, const TIn& data)
  {
    if (idx >= this->pipes_.size())
    {
      throw std::out_of_range("Index out of range");
    }

    pipes_[idx]->put(data);
  }

  TIn fetch(size_t idx)
  {
    if (idx >= this->pipes_.size())
    {
      throw std::out_of_range("Index out of range");
    }

    return this->pipes_[idx]->fetch();
  }

  void terminate()
  {
    for (auto& pipe : pipes_)
    {
      pipe->terminate();
    }
  }

  size_t get_size()
  {
    return this->pipes_.size();
  }

private:
  std::vector<std::unique_ptr<PipeDataIn<TIn>>> pipes_;
};

template <typename TIn>
class PipeDataInCollectionOnce
{
public:
  PipeDataInCollectionOnce(size_t nCams)
  {
    for (size_t i = 0; i < nCams; ++i)
    {
      pipes_.push_back(std::make_unique<PipeDataInOnce<TIn>>());
    }
  }

  void put(size_t idx, const TIn& data)
  {
    if (idx >= this->pipes_.size())
    {
      throw std::out_of_range("Index out of range");
    }
    std::cout << "put data once" << std::endl;

    pipes_[idx]->put(data);
  }

  TIn fetch(size_t idx)
  {
    if (idx >= this->pipes_.size())
    {
      throw std::out_of_range("Index out of range");
    }

    return this->pipes_[idx]->fetch();
  }

  std::vector<TIn> fetch_all()
  {
    std::vector<TIn> out;
    for (auto& pipe : pipes_)
    {
      out.push_back(pipe->fetch());
    }

    return out;
  }

  void terminate()
  {
    for (auto& pipe : pipes_)
    {
      pipe->terminate();
    }
  }

  size_t get_size()
  {
    return this->pipes_.size();
  }

private:
  std::vector<std::unique_ptr<PipeDataInOnce<TIn>>> pipes_;
};
