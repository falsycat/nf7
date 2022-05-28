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

}  // namespace nf7
