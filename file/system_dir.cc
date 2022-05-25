#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>
#include <yas/types/std/map.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/context.hh"
#include "common/dir.hh"
#include "common/gui_window.hh"
#include "common/ptr_selector.hh"
#include "common/type_info.hh"
#include "common/yas.hh"


namespace nf7 {
namespace {

class Dir final : public File,
    public nf7::Dir,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<Dir> kType = {"System/Dir", {"DirItem"}};

  using ItemMap = std::map<std::string, std::unique_ptr<File>>;

  Dir(Env& env, ItemMap&& items = {}, const gui::Window* src = nullptr) noexcept :
      File(kType, env),
      DirItem(DirItem::kTree | DirItem::kMenu | DirItem::kTooltip),
      items_(std::move(items)), win_(*this, "TreeView System/Dir", src) {
  }

  Dir(Env& env, Deserializer& ar) : Dir(env) {
    ar(items_, win_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(items_, win_);
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
  std::map<std::string, File*> FetchItems() const noexcept override {
    std::map<std::string, File*> ret;
    for (const auto& item : items_) {
      ret[item.first] = item.second.get();
    }
    return ret;
  }

  void Update() noexcept override;
  void UpdateTree() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

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
  // persistent params
  ItemMap     items_;
  gui::Window win_;
};

void Dir::Update() noexcept {
  const auto em = ImGui::GetFontSize();

  // update children
  for (const auto& item : items_) {
    ImGui::PushID(item.second.get());
    item.second->Update();
    ImGui::PopID();
  }

  // new item popup
  if (ImGui::BeginPopup("NewItemPopup")) {
    static const TypeInfo* selecting = nullptr;
    static std::string filter = "";
    static std::string name   = "";

    ImGui::PushItemWidth(16*em);
    ImGui::TextUnformatted("System/Dir: adding new item...");

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("name", &name);
    ImGui::Spacing();

    ImGui::InputTextWithHint("type", "search", &filter);
    if (ImGui::BeginListBox("##type_list", {16*em, 4*em})) {
      for (const auto& reg : registry()) {
        const auto& t = *reg.second;
        if (!t.flags().contains("DirItem")) continue;
        if (!t.flags().contains("File_Factory")) continue;

        const bool name_match =
            filter.empty() || t.name().find(filter) != std::string::npos;

        const bool sel = (selecting == &t);
        if (!name_match) {
          if (sel) selecting = nullptr;
          continue;
        }

        ImGui::PushID(&t);
        constexpr auto kFlags =
            ImGuiSelectableFlags_SpanAllColumns |
            ImGuiSelectableFlags_AllowItemOverlap;
        if (ImGui::Selectable("##selectable", sel, kFlags)) {
          selecting = &t;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(t.name().c_str());
        ImGui::PopID();
      }
      ImGui::EndListBox();
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();

    // input validation
    bool err = false;
    if (selecting == nullptr) {
      ImGui::Bullet(); ImGui::TextUnformatted("type is not selected");
      err = true;
    }
    try {
      Path::ValidateTerm(name);
    } catch (Exception& e) {
      ImGui::Bullet(); ImGui::Text("invalid name: %s", e.msg().c_str());
      err = true;
    }
    if (items_.find(name) != items_.end()) {
      ImGui::Bullet(); ImGui::Text("name duplicated");
      err = true;
    }

    if (!err) {
      if (ImGui::Button("ok")) {
        ImGui::CloseCurrentPopup();

        auto ctx  = std::make_shared<SimpleContext>(env(), id(), 0, 0, "adding new file");
        auto task = [this, name = std::move(name), type = selecting]() {
          Add(name, type->Create(env()));
        };
        env().ExecMain(ctx, std::move(task));
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "create %s as '%s' on '%s'",
            selecting->name().c_str(), name.c_str(), abspath().Stringify().c_str());
      }
    }
    ImGui::EndPopup();
  }

  // tree view window
  const auto kInit = [em]() {
    ImGui::SetNextWindowSize({8*em, 8*em}, ImGuiCond_FirstUseEver);
  };
  const char* popup = nullptr;
  if (win_.Begin(kInit)) {
    if (ImGui::BeginPopupContextWindow()) {
      if (ImGui::MenuItem("new")) {
        popup = "NewItemPopup";
      }
      ImGui::Separator();
      UpdateMenu();
      ImGui::EndPopup();
    }
    UpdateTree();
  }
  win_.End();
  if (popup) ImGui::OpenPopup(popup);
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

    const bool open = ImGui::TreeNodeEx(
        item.second.get(), flags, "%s", name.c_str());

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
        auto ctx  = std::make_shared<SimpleContext>(env(), id(), 0, 0, "removing file");
        env().ExecMain(ctx, [this, name]() { Remove(name); });
      }
      if (ImGui::MenuItem("rename")) {
        auto ctx  = std::make_shared<SimpleContext>(env(), id(), 0, 0, "renaming file");
        env().ExecMain(ctx, []() { throw Exception("not implemented"); });
      }
      if (ditem && (ditem->flags() & DirItem::kMenu)) {
        ImGui::Separator();
        ditem->UpdateMenu();
      }
      ImGui::EndPopup();
    }

    // displayed contents
    if (open) {
      ImGui::TreePush(&file);
      if (ditem && (ditem->flags() & DirItem::kTree)) {
        ditem->UpdateTree();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}
void Dir::UpdateMenu() noexcept {
  ImGui::MenuItem("TreeView", nullptr, &win_.shown());
}
void Dir::UpdateTooltip() noexcept {
  ImGui::Text("children: %zu", items_.size());
}

}
}  // namespace nf7
