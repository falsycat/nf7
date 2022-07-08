#pragma once

#include <functional>
#include <memory>

#include <miniaudio.h>

#include "nf7.hh"


namespace nf7::audio {

class Queue : public nf7::File::Interface {
 public:
  using Task = std::function<void(ma_context*)>;

  Queue() = default;
  virtual ~Queue() = default;
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  // thread-safe
  // WARNING: when failed to create ma_context, nullptr is passed
  virtual void Push(const std::shared_ptr<nf7::Context>&, Task&&) noexcept = 0;

  virtual std::shared_ptr<Queue> self() noexcept = 0;
};

}  // namespace nf7::audio
