#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>
#include <yas/types/std/map.hpp>
#include <yas/types/std/unordered_set.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/dir.hh"
#include "common/dir_item.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/gui_dnd.hh"
#include "common/gui_file.hh"
#include "common/gui_window.hh"
#include "common/ptr_selector.hh"
#include "common/yas_nf7.hh"


namespace nf7 {
namespace {

class Dir final : public File,
    public nf7::Dir,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<Dir> kType = {"System/Dir", {"DirItem"}};
  static constexpr const char* kTypeDescription = "generic directory";

  using ItemMap = std::map<std::string, std::unique_ptr<File>>;

  Dir(Env& env, ItemMap&& items = {}, const gui::Window* src = nullptr) noexcept :
      File(kType, env),
      DirItem(DirItem::kTree |
              DirItem::kMenu |
              DirItem::kTooltip |
              DirItem::kDragDropTarget),
      items_(std::move(items)), win_(*this, "TreeView System/Dir", src) {
  }

  Dir(Env& env, Deserializer& ar) : Dir(env) {
    ar(items_, opened_, win_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(items_, opened_, win_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    ItemMap items;
    for (const auto& item : items_) {
      items[item.first] = item.second->Clone(env);
    }
    return std::make_unique<Dir>(env, std::move(items));
  }

  File* Find(std::string_view name) const noexcept override {
    auto itr = items_.find(std::string(name));
    if (itr == items_.end()) return nullptr;
    return itr->second.get();
  }

  File& Add(std::string_view name, std::unique_ptr<File>&& f) override {
    const auto sname = std::string(name);

    auto [itr, ok] = items_.emplace(sname, std::move(f));
    if (!ok) throw DuplicateException("item name duplication: "+sname);

    auto& ret = *itr->second;
    if (id()) ret.MoveUnder(*this, name);
    return ret;
  }
  std::unique_ptr<File> Remove(std::string_view name) noexcept override {
    auto itr = items_.find(std::string(name));
    if (itr == items_.end()) return nullptr;

    auto ret = std::move(itr->second);
    items_.erase(itr);
    if (id()) ret->Isolate();
    return ret;
  }

  void Update() noexcept override;
  void UpdateTree() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateDragDropTarget() noexcept override;

  void Handle(const Event& ev) noexcept override {
    switch (ev.type) {
    case Event::kAdd:
      for (const auto& item : items_) item.second->MoveUnder(*this, item.first);
      break;
    case Event::kRemove:
      for (const auto& item : items_) item.second->Isolate();
      break;
    case Event::kReqFocus:
      win_.SetFocus();
      break;

    default:
      break;
    }
  }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::Dir, nf7::DirItem>(t).Select(this);
  }

 private:
  const char* popup_ = nullptr;

  std::string rename_target_;

  // persistent params
  ItemMap     items_;
  gui::Window win_;

  std::unordered_set<std::string> opened_;


  std::string GetUniqueName(std::string_view name) const noexcept {
    auto ret = std::string {name};
    while (Find(ret)) {
      ret += "_dup";
    }
    return ret;
  }
};

void Dir::Update() noexcept {
  const auto em = ImGui::GetFontSize();

  // update children
  for (const auto& item : items_) {
    ImGui::PushID(item.second.get());
    item.second->Update();
    ImGui::PopID();
  }

  if (const auto popup = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(popup);
  }

  // new item popup
  if (ImGui::BeginPopup("NewItemPopup")) {
    static nf7::gui::FileCreatePopup<
        nf7::gui::kNameInput | nf7::gui::kNameDupCheck> p(
            {"File_Factory", "DirItem"});
    ImGui::TextUnformatted("System/Dir: adding new file...");
    if (p.Update(*this)) {
      auto ctx  = std::make_shared<nf7::GenericContext>(*this, "adding new item");
      auto task = [this, name = p.name(), &type = p.type()]() {
        Add(name, type.Create(env()));
      };
      env().ExecMain(ctx, std::move(task));
    }
    ImGui::EndPopup();
  }

  // rename popup
  if (ImGui::BeginPopup("RenamePopup")) {
    static std::string new_name;
    ImGui::TextUnformatted("System/Dir: renaming an exsting item...");
    ImGui::InputText("before", &rename_target_);

    bool submit = false;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputText("after", &new_name, ImGuiInputTextFlags_EnterReturnsTrue)) {
      submit = true;
    }

    bool err = false;
    if (!Find(rename_target_)) {
      ImGui::Bullet(); ImGui::TextUnformatted("before is invalid: missing target");
      err = true;
    }
    if (Find(new_name)) {
      ImGui::Bullet(); ImGui::TextUnformatted("after is invalid: duplicated name");
      err = true;
    }
    try {
      Path::ValidateTerm(new_name);
    } catch (Exception& e) {
      ImGui::Bullet(); ImGui::Text("after is invalid: %s", e.msg().c_str());
      err = true;
    }

    if (!err) {
      if (ImGui::Button("ok")) {
        submit = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "rename '%s' to '%s' on '%s'",
            rename_target_.c_str(), new_name.c_str(), abspath().Stringify().c_str());
      }
    }

    if (submit) {
      ImGui::CloseCurrentPopup();

      auto ctx  = std::make_shared<nf7::GenericContext>(*this, "renaming item");
      auto task = [this, before = std::move(rename_target_), after = std::move(new_name)]() {
        auto f = Remove(before);
        if (!f) throw Exception("missing target");
        Add(after, std::move(f));
      };
      env().ExecMain(ctx, std::move(task));
    }
    ImGui::EndPopup();
  }

  // tree view window
  const auto kInit = [em]() {
    ImGui::SetNextWindowSize({8*em, 8*em}, ImGuiCond_FirstUseEver);
  };
  if (win_.Begin(kInit)) {
    if (ImGui::BeginPopupContextWindow()) {
      UpdateMenu();
      ImGui::EndPopup();
    }
    UpdateTree();

    if (nf7::gui::dnd::IsFirstAccept()) {
      ImGui::SetCursorPos({0, 0});
      ImGui::Dummy(ImGui::GetContentRegionAvail());
      if (ImGui::BeginDragDropTarget()) {
        UpdateDragDropTarget();
        ImGui::EndDragDropTarget();
      }
    }
  }
  win_.End();
}
void Dir::UpdateTree() noexcept {
  for (const auto& item : items_) {
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanFullWidth;

    const auto& name = item.first;
    auto&       file  = *item.second;
    ImGui::PushID(&file);

    auto* ditem = file.interface<nf7::DirItem>();
    if (ditem && !(ditem->flags() & DirItem::kTree)) {
      flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const bool opened = opened_.contains(name);
    if (opened) {
      ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    }

    const auto top  = ImGui::GetCursorPosY();
    const bool open = ImGui::TreeNodeEx(item.second.get(), flags, "%s", name.c_str());
    if (!opened && open) {
      opened_.insert(name);
    } else if (opened && !open) {
      opened_.erase(name);
    }

    // tooltip
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(file.type().name().c_str());
      ImGui::SameLine();
      ImGui::TextDisabled(file.abspath().Stringify().c_str());
      if (ditem && (ditem->flags() & DirItem::kTooltip)) {
        ImGui::Indent();
        ditem->UpdateTooltip();
        ImGui::Unindent();
      }
      ImGui::EndTooltip();
    }

    // context menu
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("copy path")) {
        ImGui::SetClipboardText(file.abspath().Stringify().c_str());
      }

      ImGui::Separator();
      if (ImGui::MenuItem("remove")) {
        env().ExecMain(
            std::make_shared<nf7::GenericContext>(*this, "removing item"),
            [this, name]() { Remove(name); });
      }
      if (ImGui::MenuItem("rename")) {
        rename_target_ = name;
        popup_         = "RenamePopup";
      }

      if (ImGui::MenuItem("renew")) {
        env().ExecMain(
            std::make_shared<nf7::GenericContext>(*this, "removing item"),
            [this, name]() { Add(name, Remove(name)); });
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("re-initialize the item by re-adding after removing");
      }

      ImGui::Separator();
      if (ImGui::MenuItem("add new sibling")) {
        popup_ = "NewItemPopup";
      }

      if (ditem && (ditem->flags() & DirItem::kMenu)) {
        ImGui::Separator();
        ditem->UpdateMenu();
      }
      ImGui::EndPopup();
    }

    // dnd source
    if (ImGui::BeginDragDropSource()) {
      gui::dnd::Send(gui::dnd::kFilePath, item.second->abspath());
      ImGui::TextUnformatted(file.type().name().c_str());
      ImGui::SameLine();
      ImGui::TextDisabled(file.abspath().Stringify().c_str());
      ImGui::EndDragDropSource();
    }

    // displayed contents
    if (open) {
      ImGui::TreePush(&file);
      if (ditem && (ditem->flags() & DirItem::kTree)) {
        ditem->UpdateTree();
      }
      ImGui::TreePop();
    }
    const auto bottom = ImGui::GetCursorPosY();

    // dnd target
    if (nf7::gui::dnd::IsFirstAccept()) {
      if (ditem && (ditem->flags() & DirItem::kDragDropTarget)) {
        ImGui::SetCursorPosY(top);
        ImGui::Dummy({ImGui::GetContentRegionAvail().x, bottom-top});
        if (ImGui::BeginDragDropTarget()) {
          ditem->UpdateDragDropTarget();
          ImGui::EndDragDropTarget();
        }
      }
    }

    ImGui::SetCursorPosY(bottom);
    ImGui::PopID();
  }
}
void Dir::UpdateMenu() noexcept {
  if (ImGui::MenuItem("add new child")) {
    popup_ = "NewItemPopup";
  }
  ImGui::Separator();
  ImGui::MenuItem("TreeView", nullptr, &win_.shown());
}
void Dir::UpdateTooltip() noexcept {
  ImGui::Text("children: %zu", items_.size());
}
void Dir::UpdateDragDropTarget() noexcept
try {
  nf7::File::Path p;
  if (auto pay = gui::dnd::Peek<Path>(gui::dnd::kFilePath, p)) {
    auto& target = ResolveOrThrow(p);
    if (target.parent() == this) {
      return;
    }

    auto parent = static_cast<nf7::File*>(this);
    while (parent) {
      if (parent == &target) return;
      parent = parent->parent();
    }

    auto& dir = target.parent()->interfaceOrThrow<nf7::Dir>();

    nf7::gui::dnd::DrawRect();
    if (pay->IsDelivery()) {
      env().ExecMain(
          std::make_shared<nf7::GenericContext>(*this, "moving an item"),
          [this, &dir, name = target.name()]() { Add(GetUniqueName(name), dir.Remove(name)); });
    }
  }
} catch (nf7::Exception&) {
}

}
}  // namespace nf7
