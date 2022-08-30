#pragma once

#include <cassert>
#include <exception>
#include <memory>
#include <mutex>
#include <string_view>

#include <source_location.hh>

#include "nf7.hh"

#include "common/file_base.hh"
#include "common/logger.hh"


namespace nf7 {

class LoggerRef final : public nf7::FileBase::Feature {
 public:
  LoggerRef(nf7::File& f, nf7::File::Path&& p = {"_logger"}) noexcept :
      file_(&f), path_(std::move(p)) {
  }
  LoggerRef(const LoggerRef&) = default;
  LoggerRef(LoggerRef&&) = default;
  LoggerRef& operator=(const LoggerRef&) = default;
  LoggerRef& operator=(LoggerRef&&) = default;

  void Handle(const nf7::File::Event& ev) noexcept override {
    std::unique_lock<std::mutex> k(mtx_);
    switch (ev.type) {
    case nf7::File::Event::kAdd:
      try {
        id_     = file_->id();
        logger_ = file_->
            ResolveUpwardOrThrow(path_).interfaceOrThrow<nf7::Logger>().self();
      } catch (nf7::Exception&) {
        id_     = 0;
        logger_ = nullptr;
      }
      break;
    case nf7::File::Event::kRemove:
      id_     = 0;
      logger_ = nullptr;
      break;
    default:
      break;
    }
  }

  // thread-safe
  void Trace(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kTrace, msg, 0, s});
  }
  void Info(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kInfo, msg, 0, s});
  }
  void Info(nf7::Exception& e, std::source_location s = std::source_location::current()) noexcept {
    Info(e.StringifyRecursive(), s);
  }
  void Warn(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kWarn, msg, 0, s});
  }
  void Warn(nf7::Exception& e, std::source_location s = std::source_location::current()) noexcept {
    Warn(e.StringifyRecursive(), s);
  }
  void Error(std::string_view msg, std::source_location s = std::source_location::current()) noexcept {
    Write({nf7::Logger::kError, msg, 0, s});
  }
  void Error(nf7::Exception& e, std::source_location s = std::source_location::current()) noexcept {
    Error(e.StringifyRecursive(), s);
  }
  void Write(nf7::Logger::Item&& item) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (!id_ || !logger_) return;
    item.file = id_;
    item.ex   = std::current_exception();
    logger_->Write(std::move(item));
  }

 private:
  nf7::File* const file_;
  const nf7::File::Path path_;

  std::mutex mtx_;
  File::Id id_;
  std::shared_ptr<nf7::Logger> logger_;
};

}  // namespace nf7
