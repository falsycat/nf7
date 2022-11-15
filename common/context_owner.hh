#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "nf7.hh"


namespace nf7 {

class ContextOwner final {
 public:
  ContextOwner() = default;
  ~ContextOwner() noexcept {
    AbortAll();
  }
  ContextOwner(const ContextOwner&) = delete;
  ContextOwner(ContextOwner&&) = default;
  ContextOwner& operator=(const ContextOwner&) = delete;
  ContextOwner& operator=(ContextOwner&&) = default;

  template <typename T, typename... Args>
  std::shared_ptr<T> Create(Args&&... args) noexcept {
    static_assert(std::is_base_of<nf7::Context, T>::value);

    ctx_.erase(
        std::remove_if(ctx_.begin(), ctx_.end(), [](auto& x) { return x.expired(); }),
        ctx_.end());

    auto ret = std::make_shared<T>(std::forward<Args>(args)...);
    ctx_.emplace_back(ret);
    return ret;
  }

  void AbortAll() noexcept {
    for (auto& wctx : ctx_) {
      if (auto ctx = wctx.lock()) {
        ctx->Abort();
      }
    }
  }

 private:
  std::vector<std::weak_ptr<nf7::Context>> ctx_;
};

}  // namespace nf7
