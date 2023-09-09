// No copyright
#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "iface/subsys/meta_env.hh"

namespace nf7::core {

class NullMetaEnv : public subsys::MetaEnv {
 public:
  static inline const auto kInstance = std::make_shared<NullMetaEnv>();

 public:
  NullMetaEnv() noexcept : subsys::MetaEnv("nf7::core::NullMetaEnv") { }

 public:
  Env* FindOr(std::string_view) const noexcept override { return nullptr; }
  std::optional<Pair> FindOr(uint64_t) const noexcept override { return std::nullopt; }
  std::vector<Pair> FetchAll() const override { return {}; }
  std::shared_ptr<subsys::MetaEnv> parent() const noexcept override { return nullptr; }
};

class MetaEnv : public subsys::MetaEnv {
 public:
  using SharedPair = std::pair<std::string, std::shared_ptr<Env>>;

 private:
  static std::vector<SharedPair>&& Sort(std::vector<SharedPair>&& v) noexcept {
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return v;
  }

 public:
  explicit MetaEnv(std::vector<SharedPair>&& children,
                   const std::weak_ptr<subsys::MetaEnv>& parent = {}) noexcept
      : subsys::MetaEnv("nf7::core::MetaEnv"),
        children_(Sort(std::move(children))),
        parent_(parent) {
    assert(std::all_of(children_.begin(), children_.end(),
                       [](auto& x) { return x.second; }));
  }

 public:
  Env* FindOr(std::string_view name) const noexcept override {
    const auto itr = std::lower_bound(
        children_.cbegin(), children_.cend(), name,
        [](const auto& a, const auto& b) { return a.first < b; });
    if (children_.cend() == itr || itr->first != name) {
      return nullptr;
    }
    return itr->second.get();
  }
  std::optional<Pair> FindOr(uint64_t index) const noexcept override {
    if (index >= children_.size()) {
      return std::nullopt;
    }
    const auto& child = children_[index];
    return Pair {child.first, *child.second};
  }

  std::vector<Pair> FetchAll() const override
  try {
    std::vector<Pair> ret;
    ret.reserve(children_.size());
    for (const auto& child : children_) {
      ret.emplace_back(child.first, *child.second);
    }
    return ret;
  } catch (const std::bad_alloc&) {
    throw MemoryException {};
  }

 public:
  std::shared_ptr<subsys::MetaEnv> parent() const noexcept override {
    return parent_.lock();
  }

 private:
  const std::vector<SharedPair> children_;
  const std::weak_ptr<subsys::MetaEnv> parent_;
};

}  // namespace nf7::core
