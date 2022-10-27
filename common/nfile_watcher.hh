#pragma once

#include <functional>
#include <filesystem>
#include <optional>
#include <vector>

#include "common/file_base.hh"


namespace nf7 {

class NFileWatcher final : public nf7::FileBase::Feature {
 public:
  NFileWatcher() = default;
  NFileWatcher(const NFileWatcher&) = delete;
  NFileWatcher(NFileWatcher&&) = delete;
  NFileWatcher& operator=(const NFileWatcher&) = delete;
  NFileWatcher& operator=(NFileWatcher&&) = delete;

  void Watch(const std::filesystem::path& npath) noexcept {
    npaths_.push_back(npath);
    lastmod_ = std::nullopt;
  }
  void Clear() noexcept {
    npaths_.clear();
    lastmod_ = std::nullopt;
  }

  std::function<void()> onMod;

 protected:
  void Update() noexcept override {
    auto latest = std::filesystem::file_time_type::duration::min();
    for (const auto& npath : npaths_) {
      try {
        const auto lastmod = std::filesystem::last_write_time(npath).time_since_epoch();
        latest = std::max(latest, lastmod);
      } catch (std::filesystem::filesystem_error&) {
      }
    }
    if (!lastmod_) {
      lastmod_ = latest;
    }
    if (*lastmod_ < latest) {
      onMod();
      lastmod_ = latest;
    }
  }

 private:
  std::vector<std::filesystem::path> npaths_;

  std::optional<std::filesystem::file_time_type::duration> lastmod_;
};

}  // namespace nf7
