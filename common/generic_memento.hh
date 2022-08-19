#pragma once

#include <cassert>
#include <memory>
#include <unordered_map>
#include <utility>

#include "common/memento.hh"


namespace nf7 {

template <typename T>
class GenericMemento : public nf7::Memento {
 public:
  class CustomTag;

  GenericMemento(T&& data) noexcept : initial_(T(data)), data_(std::move(data)) {
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
    onRestore();
  }
  void Commit() noexcept {
    tag_ = nullptr;
    onCommit();
  }
  void CommitAmend() noexcept {
    if (!tag_) return;
    auto itr = map_.find(tag_->id());
    assert(itr != map_.end());
    itr->second = data_;
    onCommit();
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
  const T initial_;
  T data_;

  Tag::Id next_ = 0;
  std::unordered_map<Tag::Id, T> map_;

  std::shared_ptr<nf7::Memento::Tag> tag_;
  std::shared_ptr<nf7::Memento::Tag> last_;
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
