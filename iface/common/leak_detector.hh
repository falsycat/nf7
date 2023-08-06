// No copyright
#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace nf7 {
#if !defined(NDEBUG)

  template <typename T>
  class LeakDetector {
   public:
    LeakDetector() { (void) checker_; ++count_; }
    virtual ~LeakDetector() noexcept { --count_; }

   private:
    struct Checker {
      ~Checker() {
        if (count_ > 0) {
          std::cerr << "LEAK DETECTED: " << typeid(T).name() << std::endl;
        }
      }
    };

   public:
    static uint64_t count() noexcept { return count_; }

   private:
    static inline Checker               checker_;
    static inline std::atomic<uint64_t> count_ = 0;
  };

#else

  template <typename T>
  class LeakDetector {
   public:
    static uint64_t count() noexcept { return 0; }
  };

#endif
}  // namespace nf7
