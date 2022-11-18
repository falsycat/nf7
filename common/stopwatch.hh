#pragma once

#include <chrono>
#include <iostream>

#include "nf7.hh"


namespace nf7 {

class Stopwatch final {
 public:
  struct Benchmark;

  Stopwatch() = default;
  Stopwatch(const Stopwatch&) = default;
  Stopwatch(Stopwatch&&) = default;
  Stopwatch& operator=(const Stopwatch&) = default;
  Stopwatch& operator=(Stopwatch&&) = default;

  void Begin() noexcept {
    begin_ = nf7::Env::Clock::now();
  }
  void End() noexcept {
    end_ = nf7::Env::Clock::now();
  }

  auto dur() const noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(end_ - begin_);
  }
  nf7::Env::Time beginTime() const noexcept { return begin_; }
  nf7::Env::Time endTime() const noexcept { return end_; }

 private:
  nf7::Env::Time begin_;
  nf7::Env::Time end_;
};
inline std::ostream& operator << (std::ostream& out, const Stopwatch& sw) {
  return out << sw.dur().count() << " usecs";
}

struct Stopwatch::Benchmark final {
 public:
  Benchmark(const char* name) noexcept : name_(name) {
    sw_.Begin();
  }
  ~Benchmark() noexcept {
    sw_.End();
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
