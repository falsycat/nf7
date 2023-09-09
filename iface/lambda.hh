// No copyright
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "iface/common/leak_detector.hh"
#include "iface/common/exception.hh"
#include "iface/common/observer.hh"
#include "iface/common/value.hh"

namespace nf7 {

class Lambda : private LeakDetector<Lambda> {
 public:
  Lambda() = default;
  virtual ~Lambda() = default;

  Lambda(const Lambda&) = delete;
  Lambda(Lambda&&) = delete;
  Lambda& operator=(const Lambda&) = delete;
  Lambda& operator=(Lambda&&) = delete;
};

}  // namespace nf7
