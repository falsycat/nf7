// No copyright
#pragma once

#include <exception>
#include <source_location>
#include <string>
#include <string_view>

#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Logger : public Interface {
 public:
  using SrcLoc = std::source_location;

  enum Level {
    kTrace,
    kInfo,
    kWarn,
    kError,
  };

  class Item final {
   public:
    Item() = delete;
    Item(Level level,
         std::string_view contents,
         std::source_location srcloc = std::source_location::current(),
         std::exception_ptr ex = std::current_exception()) noexcept
        : level_(level),
          contents_(contents),
          srcloc_(srcloc),
          exception_(ex) { }

    Item(const Item&) = default;
    Item(Item&&) = default;
    Item& operator=(const Item&) = default;
    Item& operator=(Item&&) = default;

    Level level() const noexcept { return level_; }
    const std::string& contents() const noexcept { return contents_; }
    const std::source_location& srcloc() const noexcept { return srcloc_; }
    const std::exception_ptr& exception() const noexcept { return exception_; }

   private:
    Level level_;
    std::string contents_;
    std::source_location srcloc_;
    std::exception_ptr exception_;
  };

 public:
  using Interface::Interface;

 public:
  // THREAD-SAFE
  virtual void Push(const Item&) noexcept = 0;

  // THREAD-SAFE
  void Trace(std::string_view contents,
             SrcLoc srcloc = SrcLoc::current(),
             std::exception_ptr ex = std::current_exception()) noexcept {
    Push(Item {kTrace, contents, srcloc, ex});
  }

  // THREAD-SAFE
  void Info(std::string_view contents,
            SrcLoc srcloc = SrcLoc::current(),
             std::exception_ptr ex = std::current_exception()) noexcept {
    Push(Item {kInfo, contents, srcloc, ex});
  }

  // THREAD-SAFE
  void Warn(std::string_view contents,
            SrcLoc srcloc = SrcLoc::current(),
             std::exception_ptr ex = std::current_exception()) noexcept {
    Push(Item {kWarn, contents, srcloc, ex});
  }

  // THREAD-SAFE
  void Error(std::string_view contents,
             SrcLoc srcloc = SrcLoc::current(),
             std::exception_ptr ex = std::current_exception()) noexcept {
    Push(Item {kError, contents, srcloc, ex});
  }
};

}  // namespace nf7::subsys
