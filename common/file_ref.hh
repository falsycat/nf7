#pragma once

#include <utility>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/yas_nf7.hh"


namespace nf7 {

class FileRef {
 public:
  FileRef() = delete;
  FileRef(File& owner) noexcept : owner_(&owner) {
  }
  FileRef(File& owner, const File::Path& p, File::Id id = 0) noexcept :
      owner_(&owner), path_({p}), id_(id) {
  }
  FileRef(File& owner, File::Path&& p, File::Id id = 0) noexcept :
      owner_(&owner), path_(std::move(p)), id_(id) {
  }
  FileRef(const FileRef&) = delete;
  FileRef(FileRef&&) = delete;
  FileRef& operator=(const FileRef&) = delete;
  FileRef& operator=(FileRef&&) = delete;

  File& operator*() const
  try {
    return owner_->env().GetFile(id_);
  } catch (ExpiredException&) {
    auto& ret = owner_->ResolveOrThrow(path_);
    const_cast<File::Id&>(id_) = ret.id();
    return ret;
  }

  const File::Path& path() const noexcept { return path_; }
  File::Path& path() noexcept { id_ = 0; return path_; }
  File::Id id() const { **this; return id_; }

 private:
  File* owner_;

  File::Path path_;

  File::Id id_ = 0;
};

}  // namespace nf7



namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::FileRef> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::FileRef& p) {
    ar(p.path());
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::FileRef& p) {
    ar(p.path());
    return ar;
  }
};

}  // namespace yas::detail
