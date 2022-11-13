#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>
#include <yas/types/std/unordered_set.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/config.hh"
#include "common/dir.hh"
#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_dir.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/gui_dnd.hh"
#include "common/gui_window.hh"
#include "common/ptr_selector.hh"
#include "common/yas_nf7.hh"


namespace nf7 {
namespace {

class Dir final : public nf7::FileBase,
    public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<Dir> kType = {
    "System/Dir", {"nf7::DirItem"}, "generic directory",
  };

  using ItemMap = std::map<std::string, std::unique_ptr<File>>;

  Dir(nf7::Env& env, nf7::GenericDir::ItemMap&& items = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kTree |
                   nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip |
                   nf7::DirItem::kDragDropTarget),
      dir_(*this, std::move(items)), win_(*this, "Tree View") {
    win_.onConfig = []() {
      const auto em = ImGui::GetFontSize();
      ImGui::SetNextWindowSize({8*em, 8*em}, ImGuiCond_FirstUseEver);
    };
    win_.onUpdate = [this]() { TreeView(); };
  }

  Dir(nf7::Deserializer& ar) : Dir(ar.env()) {
    ar(dir_, opened_, win_);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(dir_, opened_, win_);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Dir>(env, dir_.CloneItems(env));
  }

  void UpdateTree() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateDragDropTarget() noexcept override;

  void PostHandle(const Event& ev) noexcept override {
    switch (ev.type) {
    case Event::kAdd:
      // force to show window if this is the root
      if (name() == "$") {
        win_.Show();
      }
      return;
    default:
      return;
    }
  }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Dir, nf7::DirItem>(t).Select(this, &dir_);
  }

 private:
  nf7::GenericDir                 dir_;
  std::unordered_set<std::string> opened_;
  gui::Window                     win_;

  std::vector<std::pair<std::string, std::unique_ptr<nf7::File>>> trash_;


  static bool TestFlags(nf7::File& f, nf7::DirItem::Flags flags) noexcept
  try {
    return f.interfaceOrThrow<nf7::DirItem>().flags() & flags;
  } catch (nf7::Exception&) {
    return false;
  }

  // imgui widgets
  void TreeView() noexcept;
  void ItemAdder() noexcept;
  void ItemRenamer(const std::string& name) noexcept;
  bool ValidateName(const std::string& name) noexcept;
};

void Dir::UpdateTree() noexcept {
  for (const auto& item : dir_.items()) {
    const auto& name = item.first;
    auto&       file  = *item.second;
    ImGui::PushID(&file);

    auto* ditem = file.interface<nf7::DirItem>();
    const auto flags = ditem? ditem->flags(): 0;

    ImGuiTreeNodeFlags node_flags =
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanFullWidth;
    if (!(flags & DirItem::kTree)) {
      node_flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const bool opened = opened_.contains(name);
    if (opened) {
      ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    }

    const auto top  = ImGui::GetCursorPosY();
    const bool open = ImGui::TreeNodeEx(
        item.second.get(), node_flags, "%s", name.c_str());
    if (!opened && open) {
      opened_.insert(name);
    } else if (opened && !open) {
      opened_.erase(name);
    }

    // tooltip
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      nf7::gui::FileTooltip(file);
      ImGui::EndTooltip();
    }

    // send nf7::File::Event::kReqFocus on double click
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      env().Handle({.id = file.id(), .type = nf7::File::Event::kReqFocus});
    }

    // context menu
    if (ImGui::BeginPopupContextItem()) {
      ImGui::BeginDisabled(flags & nf7::DirItem::kImportant);
      if (ImGui::MenuItem("remove")) {
        env().ExecMain(
            std::make_shared<nf7::GenericContext>(*this, "removing item"),
            [this, name]() { trash_.emplace_back(name, dir_.Remove(name)); });
      }
      if (ImGui::BeginMenu("rename")) {
        ItemRenamer(name);
        ImGui::EndMenu();
      }

      if (ImGui::MenuItem("renew")) {
        env().ExecMain(
            std::make_shared<nf7::GenericContext>(*this, "renewing item"),
            [this, name]() { dir_.Renew(name); });
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("re-initialize the item by re-adding after removing");
      }

      if (ImGui::MenuItem("clone")) {
        env().ExecMain(
            std::make_shared<nf7::GenericContext>(*this, "duplicating item"),
            [this, name, &file]() { dir_.Add(dir_.GetUniqueName(name), file.Clone(env())); });
      }
      ImGui::EndDisabled();

      ImGui::Separator();
      nf7::gui::FileMenuItems(file);

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
      if (flags & DirItem::kTree) {
        ditem->UpdateTree();
      }
      ImGui::TreePop();
    }
    const auto bottom = ImGui::GetCursorPosY();

    // dnd target
    if (nf7::gui::dnd::IsFirstAccept()) {
      if (flags & DirItem::kDragDropTarget) {
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
  if (ImGui::BeginMenu("add new child")) {
    ItemAdder();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("restore item", trash_.size() > 0)) {
    for (auto itr = trash_.rbegin(); itr < trash_.rend();) {
      const auto  idx  = std::distance(trash_.rbegin(), itr);
      const auto& type = itr->second->type();
      const auto  id   = itr->first + " (" + type.name() + ") ##" + std::to_string(idx);

      const auto uniq = !dir_.Find(itr->first);
      if (ImGui::MenuItem(id.c_str(), nullptr, false, uniq)) {
        auto ctx = std::make_shared<nf7::GenericContext>(*this, "restoring an item");
        auto p   = std::make_shared<std::pair<std::string, std::unique_ptr<nf7::File>>>(std::move(*itr));  // this sucks
        env().ExecMain(ctx, [this, p]() mutable {
          dir_.Add(p->first, std::move(p->second));
        });

        trash_.erase(std::next(itr).base());
        itr = trash_.rbegin()+idx;
      } else {
        ++itr;
      }
    }
    ImGui::EndMenu();
  }
  ImGui::Separator();
  win_.MenuItem();
}
void Dir::UpdateTooltip() noexcept {
  ImGui::Text("children: %zu", dir_.items().size());
}
void Dir::UpdateDragDropTarget() noexcept
try {
  nf7::File::Path p;
  if (auto pay = gui::dnd::Peek<Path>(gui::dnd::kFilePath, p)) {
    auto& target = ResolveOrThrow(p);
    if (target.parent() == nullptr || target.parent() == this) {
      return;
    }

    auto& ditem =  target.interfaceOrThrow<nf7::DirItem>();
    if (ditem.flags() & nf7::DirItem::kImportant) {
      ImGui::SetTooltip("cannot move an important file");
      return;
    }

    auto parent = static_cast<nf7::File*>(this);
    while (parent) {
      if (parent == &target) return;
      parent = parent->parent();
    }

    const auto pid = target.parent()->id();
    auto&      src = target.parent()->interfaceOrThrow<nf7::Dir>();

    nf7::gui::dnd::DrawRect();
    if (pay->IsDelivery()) {
      env().ExecMain(
          std::make_shared<nf7::GenericContext>(*this, "moving an item"),
          [this, pid, &src, name = target.name()]() {
            if (env().GetFile(pid)) {
              if (auto f = src.Remove(name)) {
                dir_.Add(dir_.GetUniqueName(name), std::move(f));
              }
            }
          });
    }
  }
} catch (nf7::File::NotImplementedException&) {
  ImGui::SetTooltip("the file is not an item of nf7::Dir");
} catch (nf7::Exception&) {
}


void Dir::TreeView() noexcept {
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

void Dir::ItemAdder() noexcept {
  static const nf7::File::TypeInfo* type;
  static std::string                name;
  static bool                       type_filtered;

  if (ImGui::IsWindowAppearing()) {
    type          = nullptr;
    name          = dir_.GetUniqueName("new_file");
    type_filtered = true;
  }
  ImGui::TextUnformatted("System/Dir: adding new file...");

  const auto em = ImGui::GetFontSize();

  bool exec = false;
  if (ImGui::BeginListBox("type", {16*em, 8*em})) {
    for (auto& p : nf7::File::registry()) {
      const auto& t = *p.second;
      if (type_filtered && !t.flags().contains("nf7::DirItem")) {
        continue;
      }

      constexpr auto kFlags =
          ImGuiSelectableFlags_SpanAllColumns |
          ImGuiSelectableFlags_AllowItemOverlap;
      if (ImGui::Selectable(t.name().c_str(), type == &t, kFlags)) {
        type = &t;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        t.UpdateTooltip();
        ImGui::EndTooltip();

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          exec = true;
        }
      }
    }

    if (type_filtered) {
      ImGui::Selectable("(show all types)");
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("double click to allow you to place system files");
        ImGui::TextDisabled("  -- great power brings DESTRUCTION and CREATION");
        ImGui::EndTooltip();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          type_filtered = false;
        }
      }
    }
    ImGui::EndListBox();
  }

  ImGui::SetNextItemWidth(16*em);
  if (ImGui::InputText("name", &name, ImGuiInputTextFlags_EnterReturnsTrue)) {
    exec = true;
  }

  bool valid = ValidateName(name);
  if (type == nullptr) {
    ImGui::Bullet(); ImGui::TextUnformatted("type not selected");
    valid = false;
  }

  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("ok")) {
    exec = true;
  }
  ImGui::EndDisabled();

  if (exec && valid) {
    ImGui::CloseCurrentPopup();
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "adding new item"),
        [this]() { dir_.Add(name, type->Create(env())); });
  }
}

void Dir::ItemRenamer(const std::string& name) noexcept {
  static std::string editing_name;
  static std::string err;
  if (ImGui::IsWindowAppearing()) {
    editing_name = name;
    err          = "";
  }

  bool exec = ImGui::InputText("##name", &editing_name, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  const auto pos = ImGui::GetCursorPos();

  ImGui::NewLine();
  bool valid = ValidateName(editing_name);

  ImGui::SetCursorPos(pos);
  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("apply")) {
    exec = true;
  }
  ImGui::EndDisabled();

  if (exec && valid) {
    ImGui::CloseCurrentPopup();
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "renaming item"),
        [this, name]() { dir_.Rename(name, editing_name); });
  }
}

bool Dir::ValidateName(const std::string& name) noexcept {
  bool ret = true;

  if (Find(name)) {
    ImGui::Bullet(); ImGui::TextUnformatted("name duplicated");
    ret = false;
  }

  try {
    nf7::File::Path::ValidateTerm(name);
  } catch (nf7::Exception& e) {
    ImGui::Bullet(); ImGui::Text("invalid format: %s", e.msg().c_str());
    ret = false;
  }
  return ret;
}

}
}  // namespace nf7
