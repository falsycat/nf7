// No copyright
#pragma once

#include <memory>

#include "iface/common/exception.hh"
#include "iface/subsys/logger.hh"


namespace nf7::core {

class NullLogger : public subsys::Logger {
 public:
  static const std::shared_ptr<NullLogger>& instance()
  try {
    static const auto kInstance = std::make_shared<NullLogger>();
    return kInstance;
  } catch (const std::bad_alloc&) {
    throw MemoryException {};
  }

 public:
  NullLogger() noexcept : subsys::Logger("nf7::core::NullLogger") { }

 public:
  void Push(const Item&) noexcept override { }
};

}  // namespace nf7::core
