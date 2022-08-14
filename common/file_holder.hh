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
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/file_base.hh"
#include "common/generic_watcher.hh"
#include "common/gui_file.hh"
#include "common/gui_popup.hh"
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

  FileHolder(nf7::File& owner, std::string_view id, const FileHolder* src = nullptr);
  FileHolder(const FileHolder&) = delete;
  FileHolder(FileHolder&&) = delete;
  FileHolder& operator=(const FileHolder&) = delete;
  FileHolder& operator=(FileHolder&&) = delete;

  // yas usertype serializer
  void serialize(auto& ar) {
    ar(entity_);
  }

  void Emplace(nf7::File::Path&& path) noexcept {
    TearDown();
    entity_ = std::move(path);
    tag_    = nullptr;
    SetUp();
  }
  void Emplace(std::unique_ptr<nf7::File>&& f) noexcept {
    TearDown();
    entity_ = std::move(f);
    tag_    = nullptr;
    SetUp();
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

  std::string GetDisplayText() const noexcept;
  void UpdateButton(bool small = false) noexcept;
  void UpdateLabel(const char* name) noexcept;

  bool own() const noexcept {
    return std::holds_alternative<std::shared_ptr<nf7::File>>(entity_);
  }
  bool ref() const noexcept {
    return std::holds_alternative<nf7::File::Path>(entity_);
  }
  bool empty() const noexcept {
    return std::holds_alternative<std::monostate>(entity_);
  }

  nf7::File* file() const noexcept { return file_; }
  nf7::File::Path path() const noexcept {
    assert(!empty());
    return own()? nf7::File::Path {{id_}}: std::get<nf7::File::Path>(entity_);
  }

  // called when memento of owned file is changed, or target is changed by GUI.
  // Emplace() doesn't.
  std::function<void(void)> onChange = [](){};

 private:
  nf7::File* const  owner_;
  const std::string id_;

  bool ready_ = false;  // whether owner is added to file tree

  Entity entity_;
  std::shared_ptr<nf7::Memento::Tag> tag_;

  nf7::File* file_ = nullptr;

  std::optional<nf7::GenericWatcher> watcher_;


  // GUI popup
  struct ConfigPopup final : nf7::gui::Popup {
   public:
    ConfigPopup(FileHolder& h) noexcept :
        nf7::gui::Popup("ConfigPopup"),
        h_(&h),
        factory_(*h.owner_, [](auto&) { return true; }) {
    }

    void Open() noexcept;
    void Update() noexcept;

   private:
    FileHolder* const h_;

    uint32_t              type_;
    std::string           path_;
    nf7::gui::FileFactory factory_;
  } popup_config_;


  void SetUp() noexcept;
  void TearDown() noexcept;
};

// to save/restore FileHolder's changes through GenericMemento
class FileHolder::Tag final {
 public:
  Tag(nf7::FileHolder& target) noexcept : target_(&target) {
  }
  Tag(const Tag&) noexcept;
  Tag& operator=(const Tag&) noexcept;
  Tag(Tag&&) = default;
  Tag& operator=(Tag&&) = default;

 private:
  nf7::FileHolder* target_ = nullptr;

  Entity entity_;
  std::shared_ptr<nf7::Memento::Tag> tag_;
};

}  // namespace nf7
