#pragma once

#include <optional>
#include <utility>

#include "nf7.hh"


namespace nf7 {

class FileRef final {
 public:
  FileRef() = delete;
  FileRef(File& owner) noexcept : owner_(&owner) {
  }
  FileRef(File& owner, File::Path&& p, File::Id id = 0) noexcept :
      owner_(&owner), path_(std::move(p)), id_(id) {
  }
  FileRef(File& owner, File::Id id) noexcept : owner_(&owner), id_(id) {
  }
  FileRef(const FileRef&) = default;
  FileRef(FileRef&&) = default;
  FileRef& operator=(const FileRef&) = default;
  FileRef& operator=(FileRef&&) = default;

  File& operator*() const {
    try {
      return owner_->env().GetFile(id_);
    } catch (ExpiredException&) {
      if (!path_) throw;
    }
    auto& ret = owner_->Resolve(*path_);
    const_cast<File::Id&>(id_) = ret.id();
    return ret;
  }
  const File::Path& path() const noexcept {
    assert(path_);
    return *path_;
  }

 private:
  File* file_;

  std::optional<File::Path> path_;

  File::Id id_ = 0;
};

}  // namespace nf7
