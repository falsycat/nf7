#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <yas/serialize.hpp>
#include <yas/types/std/map.hpp>

#include "nf7.hh"

#include "common/dir.hh"
#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/yas_nf7.hh"


namespace nf7 {

class GenericDir : public nf7::FileBase::Feature, public nf7::Dir {
 public:
  using ItemMap = std::map<std::string, std::unique_ptr<nf7::File>>;

  GenericDir() = delete;
  GenericDir(nf7::FileBase& f, ItemMap&& items = {}) noexcept :
      nf7::FileBase::Feature(f), f_(f), items_(std::move(items)) {
  }
  GenericDir(const GenericDir&) = delete;
  GenericDir(GenericDir&&) = delete;
  GenericDir& operator=(const GenericDir&) = delete;
  GenericDir& operator=(GenericDir&&) = delete;

  void serialize(auto& ar) { ar(items_); }

  ItemMap CloneItems(nf7::Env& env) const {
    ItemMap ret;
    for (auto& p : items_) {
      ret[p.first] = p.second->Clone(env);
    }
    return ret;
  }
  std::string GetUniqueName(std::string_view name) const noexcept {
    auto ret = std::string {name};
    while (items_.end() != items_.find(ret)) {
      ret += "_dup";
    }
    return ret;
  }

  nf7::File* Find(std::string_view name) const noexcept override {
    auto itr = items_.find(std::string {name});
    return itr != items_.end()? itr->second.get(): nullptr;
  }
  nf7::File& Add(std::string_view name, std::unique_ptr<File>&& f) override {
    const auto sname = std::string(name);

    auto [itr, ok] = items_.emplace(sname, std::move(f));
    if (!ok) throw nf7::Dir::DuplicateException {"item name duplication: "+sname};

    auto& ret = *itr->second;
    if (f_.id()) ret.MoveUnder(f_, name);
    return ret;
  }
  std::unique_ptr<nf7::File> Remove(std::string_view name) noexcept override {
    auto itr = items_.find(std::string(name));
    if (itr == items_.end()) return nullptr;

    auto ret = std::move(itr->second);
    items_.erase(itr);
    if (f_.id()) ret->Isolate();
    return ret;
  }

  nf7::File* Rename(std::string_view before, std::string_view after) noexcept {
    if (auto f = Remove(before)) {
      return &Add(after, std::move(f));
    } else {
      return nullptr;
    }
  }
  nf7::File* Renew(std::string_view name) noexcept {
    return Rename(name, name);
  }

  const ItemMap& items() const noexcept { return items_; }

 private:
  nf7::FileBase& f_;

  ItemMap items_;


  void Update() noexcept override {
    UpdateChildren(true);
    UpdateChildren(false);
  }
  void UpdateChildren(bool early) noexcept {
    for (auto& p : items_) {
      auto& f     = *p.second;
      auto* ditem = f.interface<nf7::DirItem>();

      const bool e = ditem && (ditem->flags() & nf7::DirItem::kEarlyUpdate);
      if (e == early) f.Update();
    }
  }
  void Handle(const nf7::File::Event& e) noexcept override {
    switch (e.type) {
    case nf7::File::Event::kAdd:
      for (auto& p : items_) p.second->MoveUnder(f_, p.first);
      return;
    case nf7::File::Event::kRemove:
      for (auto& p : items_) p.second->Isolate();
      return;
    default:
      return;
    }
  }
};

}  // namespace nf7
