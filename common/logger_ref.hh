#pragma once

#include <cassert>
#include <memory>
#include <source_location>
#include <string_view>

#include "nf7.hh"

#include "common/logger.hh"


namespace nf7 {

class LoggerRef final {
 public:
  LoggerRef() noexcept {
  }
  LoggerRef(const LoggerRef&) = default;
  LoggerRef(LoggerRef&&) = default;
  LoggerRef& operator=(const LoggerRef&) = default;
  LoggerRef& operator=(LoggerRef&&) = default;

  void SetUp(nf7::File& f, std::string_view name = "_logger") noexcept {
    try {
      id_ = f.id();
      logger_ = f.ResolveUpwardOrThrow(name).
          interfaceOrThrow<nf7::Logger>().self();
    } catch (Exception&) {
      id_ = 0;
    }
  }
  void TearDown() noexcept {
    id_     = 0;
    logger_ = nullptr;
  }

  // thread-safe
  void Trace(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kTrace, msg, 0, s});
  }
  void Info(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kInfo, msg, 0, s});
  }
  void Warn(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kWarn, msg, 0, s});
  }
  void Error(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kError, msg, 0, s});
  }
  void Write(nf7::Logger::Item&& item) noexcept {
    if (!id_ || !logger_) return;
    item.file = id_;
    logger_->Write(std::move(item));
  }

 private:
  File::Id id_;

  std::shared_ptr<nf7::Logger> logger_;
};

}  // namespace nf7
