#pragma once

#include <cassert>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "nf7.hh"

#include "common/logger.hh"


namespace nf7 {

class LoggerPool : public Logger {
 public:
  static constexpr size_t kMaxPool = 1024;
  static constexpr auto   kDefaultLoggerName = "_logger";

  LoggerPool(File&            owner,
             size_t           search_min_dist = 0,
             std::string_view name = kDefaultLoggerName) noexcept :
      owner_(&owner), search_min_dist_(search_min_dist), logger_name_(name) {
  }
  ~LoggerPool() noexcept {
    assert(items_.empty());
  }
  LoggerPool(const LoggerPool&) = delete;
  LoggerPool(LoggerPool&&) = delete;
  LoggerPool& operator=(const LoggerPool&) = delete;
  LoggerPool& operator=(LoggerPool&&) = delete;

  void Write(Logger::Item&& item) noexcept override {
    if (items_.size() >= kMaxPool) return;
    items_.push_back(std::move(item));
  }
  void Flush() noexcept
  try {
    if (items_.empty()) return;
    auto& logger = owner_->
        ancestorOrThrow(search_min_dist_).
        ResolveUpwardOrThrow(kDefaultLoggerName).
        ifaceOrThrow<nf7::Logger>();
    for (auto& item : items_) {
      item.file = owner_->id();
      logger.Write(std::move(item));
    }
    items_.clear();
  } catch (File::NotFoundException&) {
  } catch (File::NotImplementedException&) {
  }

 private:
  File* const owner_;

  size_t search_min_dist_;

  std::string logger_name_;

  std::vector<Item> items_;
};

class LoggerSyncPool : private LoggerPool {
 public:
  using LoggerPool::LoggerPool;

  void Write(Logger::Item&& item) noexcept override {
    std::unique_lock<std::mutex> _(mtx_);
    LoggerPool::Write(std::move(item));
  }
  using LoggerPool::Flush;

 private:
  std::mutex mtx_;
};

}  // namespace nf7
