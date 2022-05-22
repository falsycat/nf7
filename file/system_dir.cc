#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <yas/serialize.hpp>
#include <yas/types/std/map.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/dir.hh"
#include "common/ptr_selector.hh"
#include "common/type_info.hh"
#include "common/yas.hh"


namespace nf7 {
namespace {

class Dir final : public File, public nf7::Dir {
 public:
  static inline const GenericTypeInfo<Dir> kType = {"System", "Dir", {"DirItem"}};

  using ItemMap = std::map<std::string, std::unique_ptr<File>>;

  Dir(Env& env, ItemMap&& items = {}, bool shown = false) noexcept :
      File(kType, env), items_(std::move(items)), shown_(shown) {
  }

  Dir(Env& env, Deserializer& ar) : Dir(env) {
    ar(items_, shown_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(items_, shown_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    ItemMap items;
    for (const auto& item : items_) {
      items[item.first] = item.second->Clone(env);
    }
    return std::make_unique<Dir>(env, std::move(items));
  }

  void MoveUnder(File::Id parent) noexcept override {
    File::MoveUnder(parent);
    for (const auto& item : items_) {
      item.second->MoveUnder(id());
    }
  }

  File& Add(std::string_view name, std::unique_ptr<File>&& f) override {
    const auto sname = std::string(name);

    auto [itr, ok] = items_.emplace(sname, std::move(f));
    if (!ok) throw DuplicateException("item name duplication: "+sname);

    auto& ret = *itr->second;
    if (id()) ret.MoveUnder(id());
    return ret;
  }
  std::unique_ptr<File> Remove(std::string_view name) noexcept override {
    auto itr = items_.find(std::string(name));
    if (itr == items_.end()) return nullptr;

    auto ret = std::move(itr->second);
    items_.erase(itr);
    if (id()) ret->MoveUnder(0);
    return ret;
  }
  std::map<std::string, File*> FetchItems() const noexcept override {
    std::map<std::string, File*> ret;
    for (const auto& item : items_) {
      ret[item.first] = item.second.get();
    }
    return ret;
  }

  void Update() noexcept override;

  File::Interface* iface(const std::type_info& t) noexcept override {
    return PtrSelector<nf7::Dir>(t).Select(this);
  }

 private:
  ItemMap items_;

  bool shown_;
};

void Dir::Update() noexcept {
}

}
}  // namespace nf7
