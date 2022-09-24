#pragma once

#include <functional>
#include <filesystem>
#include <optional>

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
    npath_   = npath;
    lastmod_ = std::nullopt;
  }

  std::function<void()> onMod;

 protected:
  void Update() noexcept override
  try {
    if (npath_) {
      const auto lastmod = std::filesystem::last_write_time(*npath_);
      if (lastmod_ && lastmod > *lastmod_) {
        onMod();
      }
      lastmod_ = lastmod;
    }
  } catch (std::filesystem::filesystem_error&) {
    lastmod_.emplace();
  }

 private:
  std::optional<std::filesystem::path> npath_;
  std::optional<std::filesystem::file_time_type> lastmod_;
};

}  // namespace nf7
