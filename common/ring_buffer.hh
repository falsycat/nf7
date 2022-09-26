#pragma once

#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>


namespace nf7 {

class RingBuffer final {
 public:
  RingBuffer() = delete;
  RingBuffer(uint64_t unit, uint64_t bufn) noexcept :
      buf_(unit*bufn), unit_(unit), bufn_(bufn) {
  }
  RingBuffer(const RingBuffer&) = delete;
  RingBuffer(RingBuffer&&) = default;
  RingBuffer& operator=(const RingBuffer&) = delete;
  RingBuffer& operator=(RingBuffer&&) = default;

  template <typename T>
  uint64_t Mix(uint64_t begin, const T* ptr, uint64_t n) noexcept {
    assert(unit_ == sizeof(T));

    if (begin < cur_) {
      const auto drop = cur_ - begin;
      if (drop >= n) {
        return cur_;
      }
      ptr   += drop;
      n     -= drop;
      begin  = cur_;
    }
    if (begin > cur_) {
      const auto skip = begin - cur_;
      n = std::min(bufn_ - skip, n);
    }
    auto buf = reinterpret_cast<T*>(buf_.data());

    const auto [c, r, l] = CalcCursor(begin, n);
    for (uint64_t i = 0; i < r; ++i) {
      buf[c+i] += ptr[i];
    }
    for (uint64_t i = 0; i < l; ++i) {
      buf[i] += ptr[r+i];
    }
    return begin + n;
  }
  void Take(uint8_t* ptr, uint64_t n) noexcept {
    const auto [c, r, l] = CalcCursor(cur_, n);
    std::memcpy(&ptr[0*unit_], &buf_[c*unit_], r*unit_);
    std::memcpy(&ptr[r*unit_], &buf_[0*unit_], l*unit_);
    std::memset(&buf_[c*unit_], 0, r*unit_);
    std::memset(&buf_[0*unit_], 0, l*unit_);
    cur_ += n;
  }

  uint64_t Peek(uint64_t begin, uint8_t* ptr, uint64_t n) noexcept {
    if (cur_ > bufn_) {
      const auto actual_begin = std::max(begin, cur_-bufn_);
      const auto pad          = std::min(n, actual_begin - begin);
      std::memset(ptr, 0, pad*unit_);
      begin = actual_begin;
      ptr  += pad*unit_;
      n    -= pad;
    }
    n = std::min(n, bufn_);

    const auto [c, r, l] = CalcCursor(begin, n);
    std::memcpy(&ptr[0*unit_], &buf_[c*unit_], r*unit_);
    std::memcpy(&ptr[r*unit_], &buf_[0*unit_], l*unit_);
    return begin + n;
  }
  void Write(const uint8_t* ptr, uint64_t n) noexcept {
    const auto [c, r, l] = CalcCursor(cur_, n);
    std::memcpy(&buf_[c*unit_], &ptr[0*unit_], r*unit_);
    std::memcpy(&buf_[0*unit_], &ptr[r*unit_], l*unit_);
    cur_ += n;
  }

  uint64_t unit() const noexcept { return unit_; }
  uint64_t bufn() const noexcept { return bufn_; }
  uint64_t cur() const noexcept { return cur_; }

 private:
  std::vector<uint8_t> buf_;
  uint64_t unit_;
  uint64_t bufn_;
  uint64_t cur_ = 0;

  std::tuple<uint64_t, uint64_t, uint64_t> CalcCursor(
      uint64_t t, uint64_t n) noexcept {
    assert(n <= bufn_);
    const auto c = t % bufn_;
    const auto r = std::min(bufn_ - c, n);
    const auto l = n > r? n - r: 0;
    return {c, r, l};
  }
};

}  // namespace nf7
