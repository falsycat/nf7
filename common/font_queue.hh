#pragma once

#include <functional>
#include <memory>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "nf7.hh"


namespace nf7::font {

class Queue : public nf7::File::Interface {
 public:
  using Task = std::function<void(FT_Library)>;
  Queue() = default;
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  // thread-safe
  virtual void Push(const std::shared_ptr<nf7::Context>&, Task&&) noexcept = 0;

  virtual std::shared_ptr<Queue> self() noexcept = 0;
};

}  // namespace nf7::font
