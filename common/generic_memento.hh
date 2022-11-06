#pragma once

#include <cassert>
#include <memory>
#include <unordered_map>
#include <utility>

#include "nf7.hh"

#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/memento.hh"


namespace nf7 {

template <typename T>
class GenericMemento : public nf7::FileBase::Feature, public nf7::Memento {
 public:
  class CustomTag;

  GenericMemento(nf7::FileBase& f, T&& data) noexcept :
      nf7::FileBase::Feature(f),
      file_(f), initial_(T(data)), data_(std::move(data)) {
  }
  ~GenericMemento() noexcept {
    tag_  = nullptr;
    last_ = nullptr;
    assert(map_.empty());
  }

  T* operator->() noexcept {
    return &data_;
  }
  const T* operator->() const noexcept {
    return &data_;
  }

  std::shared_ptr<Tag> Save() noexcept override {
    if (tag_) return tag_;
    auto [itr, emplaced] = map_.emplace(next_++, data_);
    assert(emplaced);
    return last_ = tag_ = std::make_shared<CustomTag>(*this, itr->first);
  }
  void Restore(const std::shared_ptr<Tag>& tag) override {
    assert(tag);
    auto itr = map_.find(tag->id());
    assert(itr != map_.end());
    data_ = itr->second;
    tag_  = tag;
    last_ = tag;
    onRestore();
    file_.Touch();
  }
  void Commit(bool quiet = false) noexcept {
    tag_ = nullptr;
    onCommit();
    if (!quiet) file_.Touch();
  }
  void CommitQuiet() noexcept {
    Commit(true);
  }
  void CommitAmend(bool quiet = false) noexcept {
    if (!tag_) return;
    auto itr = map_.find(tag_->id());
    assert(itr != map_.end());
    itr->second = data_;
    onCommit();
    if (!quiet) file_.Touch();
  }
  void CommitAmendQuiet() noexcept {
    CommitAmend(true);
  }

  T& data() noexcept { return data_; }
  const T& data() const noexcept { return data_; }

  const T& last() const noexcept {
    if (!last_) return initial_;

    auto itr = map_.find(last_->id());
    assert(itr != map_.end());
    return itr->second;
  }

  std::function<void()> onRestore = [](){};
  std::function<void()> onCommit  = [](){};

 private:
  nf7::File& file_;

  const T initial_;
  T data_;

  Tag::Id next_ = 0;
  std::unordered_map<Tag::Id, T> map_;

  std::shared_ptr<nf7::Memento::Tag> tag_;
  std::shared_ptr<nf7::Memento::Tag> last_;


  void Handle(const nf7::File::Event& e) noexcept override {
    switch (e.type) {
    case nf7::File::Event::kAdd:
      file_.env().ExecMain(
          std::make_shared<nf7::GenericContext>(file_),
          [this]() { CommitQuiet(); });
      return;
    default:
      return;
    }
  }
};

template <typename T>
class GenericMemento<T>::CustomTag final : public Tag {
 public:
  CustomTag(GenericMemento& owner, Id id) noexcept : Tag(id), owner_(&owner) {
  }
  ~CustomTag() noexcept {
    owner_->map_.erase(id());
  }
 private:
  GenericMemento* owner_;
};

}  // namespace nf7
