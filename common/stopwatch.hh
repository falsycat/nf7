#pragma once

#include <chrono>
#include <iostream>

#include "nf7.hh"


namespace nf7 {

class Stopwatch final {
 public:
  struct Benchmark;

  static nf7::Env::Time now() noexcept { return nf7::Env::Clock::now(); }

  Stopwatch() noexcept : begin_(now()) {
  }
  Stopwatch(const Stopwatch&) = default;
  Stopwatch(Stopwatch&&) = default;
  Stopwatch& operator=(const Stopwatch&) = default;
  Stopwatch& operator=(Stopwatch&&) = default;

  auto dur() const noexcept {
    return now() - begin_;
  }

 private:
  nf7::Env::Time begin_;
};
inline std::ostream& operator << (std::ostream& out, const Stopwatch& sw) {
  return out << std::chrono::duration_cast<std::chrono::microseconds>(sw.dur()).count() << " usecs";
}

struct Stopwatch::Benchmark final {
 public:
  Benchmark(const char* name) noexcept : name_(name) {
  }
  ~Benchmark() noexcept {
    std::cout << name_ << ": " << sw_ << std::endl;
  }
  Benchmark(const Benchmark&) = delete;
  Benchmark(Benchmark&&) = delete;
  Benchmark& operator=(const Benchmark&) = delete;
  Benchmark& operator=(Benchmark&&) = delete;

 private:
  const char* name_;
  Stopwatch   sw_;
};

}  // namespace nf7
