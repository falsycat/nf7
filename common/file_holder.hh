#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <yas/serialize.hpp>
#include <yas/types/std/variant.hpp>

#include "nf7.hh"

#include "common/file_base.hh"
#include "common/generic_watcher.hh"
#include "common/memento.hh"
#include "common/yas_nf7.hh"
#include "common/yas_std_variant.hh"


namespace nf7 {

class FileHolder : public nf7::FileBase::Feature {
 public:
  class Tag;

  class EmptyException final : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  using Entity = std::variant<
      std::monostate, nf7::File::Path, std::shared_ptr<nf7::File>>;

  FileHolder(nf7::File& owner, std::string_view id) noexcept :
      owner_(&owner), id_(id) {
  }
  FileHolder(const FileHolder&) = delete;
  FileHolder(FileHolder&&) = delete;
  FileHolder& operator=(const FileHolder&) = delete;
  FileHolder& operator=(FileHolder&&) = delete;

  void Serialize(auto& ar) const {
    ar(entity_);
  }
  void Deserialize(auto& ar) {
    ar(entity_);
    SetUp();
  }

  void Emplace(nf7::File::Path&& path) noexcept {
    TearDown();
    entity_ = std::move(path);
    tag_    = nullptr;
    SetUp();

    onEmplace();
  }
  void Emplace(std::unique_ptr<nf7::File>&& f) noexcept {
    TearDown();
    entity_ = std::move(f);
    tag_    = nullptr;
    SetUp();

    onEmplace();
  }

  nf7::File& GetFileOrThrow() {
    SetUp();
    if (!file_) {
      throw EmptyException {"holder is empty"};
    }
    return *file_;
  }

  // nf7::FileBase::Feature methods
  nf7::File* Find(std::string_view name) const noexcept override;
  void Handle(const nf7::File::Event&) noexcept override;
  void Update() noexcept override;

  bool own() const noexcept {
    return std::holds_alternative<std::shared_ptr<nf7::File>>(entity_);
  }
  bool ref() const noexcept {
    return std::holds_alternative<nf7::File::Path>(entity_);
  }
  bool empty() const noexcept {
    return std::holds_alternative<std::monostate>(entity_);
  }

  nf7::File& owner() const noexcept { return *owner_; }
  nf7::Env& env() const noexcept { return owner_->env(); }
  const std::string& id() const noexcept { return id_; }

  nf7::File* file() const noexcept { return file_; }
  nf7::File::Path path() const noexcept {
    assert(!empty());
    return own()? nf7::File::Path {{id_}}: std::get<nf7::File::Path>(entity_);
  }

  // called when child's memento tag id is changed
  std::function<void(void)> onChildMementoChange = [](){};

  // called right before returning from Emplace()
  std::function<void(void)> onEmplace = [](){};

 private:
  nf7::File* const  owner_;
  const std::string id_;

  Entity entity_;
  std::shared_ptr<nf7::Memento::Tag> tag_;

  nf7::File* file_ = nullptr;

  std::optional<nf7::GenericWatcher> watcher_;


  void SetUp() noexcept;
  void TearDown() noexcept;
};

// to save/restore FileHolder's changes through GenericMemento
class FileHolder::Tag final {
 public:
  Tag() = default;
  Tag(const Tag&) noexcept;
  Tag& operator=(const Tag&) noexcept;
  Tag(Tag&&) = default;
  Tag& operator=(Tag&&) = default;

  void SetTarget(nf7::FileHolder& h) noexcept;

 private:
  nf7::FileHolder* target_ = nullptr;

  Entity entity_;
  std::shared_ptr<nf7::Memento::Tag> tag_;
};

}  // namespace nf7
 

namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::FileHolder> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::FileHolder& h) {
    h.Serialize(ar);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::FileHolder& h) {
    h.Deserialize(ar);
    return ar;
  }
};

}  // namespace yas::detail
