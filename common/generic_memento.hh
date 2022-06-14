#pragma once

#include <cassert>
#include <memory>
#include <unordered_map>
#include <utility>

#include "common/memento.hh"


namespace nf7 {

template <typename T>
class GenericMemento : public Memento {
 public:
  class CustomTag;

  GenericMemento(File& owner, T&& data) noexcept :
      owner_(&owner), initial_(T(data)), data_(std::move(data)) {
  }
  ~GenericMemento() noexcept {
    tag_  = nullptr;
    last_ = nullptr;
    assert(map_.empty());
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

    if (owner_->id()) {
      owner_->env().Handle(
          {.id = owner_->id(), .type = File::Event::kUpdate});
    }
  }
  void Commit() noexcept {
    tag_ = nullptr;
    NotifyUpdate();
  }
  void CommitAmend() noexcept {
    if (!tag_) return;
    auto itr = map_.find(tag_->id());
    assert(itr != map_.end());
    itr->second = data_;
    NotifyUpdate();
  }

  T& data() noexcept { return data_; }
  const T& data() const noexcept { return data_; }

  const T& last() const noexcept {
    if (!last_) return initial_;

    auto itr = map_.find(last_->id());
    assert(itr != map_.end());
    return itr->second;
  }

 private:
  File* const owner_;

  const T initial_;
  T data_;

  Tag::Id next_ = 0;
  std::unordered_map<Tag::Id, T> map_;

  std::shared_ptr<nf7::Memento::Tag> tag_;
  std::shared_ptr<nf7::Memento::Tag> last_;

  void NotifyUpdate() noexcept {
    if (owner_->id()) {
      owner_->env().Handle(
          {.id = owner_->id(), .type = File::Event::kUpdate});
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
