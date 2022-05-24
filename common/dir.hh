#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "nf7.hh"


namespace nf7 {

class Dir : public File::Interface {
 public:
  class DuplicateException;

  Dir() = default;

  virtual File& Add(std::string_view, std::unique_ptr<File>&&) = 0;
  virtual std::unique_ptr<File> Remove(std::string_view) noexcept = 0;

  virtual std::map<std::string, File*> FetchItems() const noexcept = 0;
};
class Dir::DuplicateException : public Exception {
 public:
  using Exception::Exception;
};

class DirItem : public File::Interface {
 public:
  enum Flag : uint8_t {
    kNone    = 0,
    kTree    = 1 << 0,
    kMenu    = 1 << 1,
    kTooltip = 1 << 2,
  };
  using Flags = uint8_t;

  DirItem() = delete;
  DirItem(Flags flags) noexcept : flags_(flags) {
  }
  DirItem(const DirItem&) = delete;
  DirItem(DirItem&&) = delete;
  DirItem& operator=(const DirItem&) = delete;
  DirItem& operator=(DirItem&&) = delete;

  virtual void UpdateTree() noexcept { }
  virtual void UpdateMenu() noexcept { }
  virtual void UpdateTooltip() noexcept { }

  Flags flags() const noexcept { return flags_; }

 private:
  Flags flags_;
};

}  // namespace nf7
