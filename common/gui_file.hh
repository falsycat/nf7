#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "nf7.hh"

#include "common/file_holder.hh"


namespace nf7::gui {

class FileFactory final {
 public:
  enum Flag : uint8_t {
    kNameInput    = 1 << 0,
    kNameDupCheck = 1 << 1,
  };
  using Flags  = uint8_t;
  using Filter = std::function<bool(const nf7::File::TypeInfo&)>;

  FileFactory(nf7::File& owner, Filter&& filter, Flags flags = 0) noexcept :
      owner_(&owner), filter_(std::move(filter)), flags_(flags) {
  }
  FileFactory(const FileFactory&) = delete;
  FileFactory(FileFactory&&) = default;
  FileFactory& operator=(const FileFactory&) = delete;
  FileFactory& operator=(FileFactory&&) = delete;

  bool Update() noexcept;
  std::unique_ptr<nf7::File> Create(nf7::Env& env) noexcept {
    return type_? type_->Create(env): nullptr;
  }

  const std::string& name() const noexcept { return name_; }
  const nf7::File::TypeInfo& type() const noexcept { return *type_; }

 private:
  nf7::File* const owner_;
  const Filter     filter_;
  const Flags      flags_;

  std::string name_;
  const nf7::File::TypeInfo* type_ = nullptr;
  std::string type_filter_;
};

class FileHolderEditor final {
 public:
  enum Type {
    kOwn,
    kRef,
  };

  FileHolderEditor(nf7::File& owner, FileFactory::Filter&& filter) noexcept :
      owner_(&owner), factory_(owner, std::move(filter)) {
  }
  FileHolderEditor(const FileHolderEditor&) = delete;
  FileHolderEditor(FileHolderEditor&&) = default;
  FileHolderEditor& operator=(const FileHolderEditor&) = delete;
  FileHolderEditor& operator=(FileHolderEditor&&) = delete;

  void Reset(nf7::FileHolder& h) noexcept;
  void Apply(nf7::FileHolder& h) noexcept;

  bool Update() noexcept;

 private:
  nf7::File* const owner_;

  Type        type_ = kOwn;
  FileFactory factory_;
  std::string path_;
};

}  // namespace nf7::gui
