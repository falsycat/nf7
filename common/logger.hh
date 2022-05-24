#pragma once

#include <string>
#include <string_view>

#include <source_location.hh>

#include "nf7.hh"


namespace nf7 {

class Logger : public File::Interface {
 public:
  enum Level {
    kTrace,
    kInfo,
    kWarn,
    kError,
  };
  struct Item;

  Logger() = default;

  virtual void Write(Item&&) noexcept = 0;
};
struct Logger::Item final {
 public:
  Item(Level lv, std::string_view m, File::Id f = 0, std::source_location s = std::source_location::current()) noexcept :
      level(lv), msg(m), file(f), srcloc(s) {
  }
  Item(const Item&) = default;
  Item(Item&&) = default;
  Item& operator=(const Item&) = default;
  Item& operator=(Item&&) = default;

  Level level;
  std::string msg;

  File::Id file;
  std::source_location srcloc;
};

}  // namespace nf7
