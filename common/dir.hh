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
  virtual ~Dir() = default;
  Dir(const Dir&) = default;
  Dir(Dir&&) = default;
  Dir& operator=(const Dir&) = default;
  Dir& operator=(Dir&&) = default;

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
  DirItem() = default;
  virtual ~DirItem() = default;
  DirItem(const DirItem&) = delete;
  DirItem(DirItem&&) = delete;
  DirItem& operator=(const DirItem&) = delete;
  DirItem& operator=(DirItem&&) = delete;

  virtual void UpdateTree() noexcept { }
  virtual void UpdateMenu() noexcept { }
  virtual void UpdateTooltip() noexcept { }
};

}  // namespace nf7
