#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>
#include <yas/types/std/unordered_set.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/dir.hh"
#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/gui_dnd.hh"
#include "common/gui_file.hh"
#include "common/gui_popup.hh"
#include "common/gui_window.hh"
#include "common/ptr_selector.hh"
#include "common/yas_nf7.hh"


namespace nf7 {
namespace {

class Dir final : public nf7::FileBase,
    public nf7::Dir,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<Dir> kType = {"System/Dir", {"nf7::DirItem"}};
  static constexpr const char* kTypeDescription = "generic directory";

  using ItemMap = std::map<std::string, std::unique_ptr<File>>;

  Dir(nf7::Env& env, ItemMap&& items = {}, const gui::Window* src = nullptr) noexcept :
      nf7::FileBase(kType, env, {&widget_popup_, &add_popup_, &rename_popup_}),
      nf7::DirItem(nf7::DirItem::kTree |
                   nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip |
                   nf7::DirItem::kDragDropTarget),
      items_(std::move(items)), win_(*this, "TreeView System/Dir", src),
      widget_popup_(*this), add_popup_(*this), rename_popup_(*this) {
  }

  Dir(nf7::Deserializer& ar) : Dir(ar.env()) {
    ar(opened_, win_);

    uint64_t size;
    ar(size);
    for (size_t i = 0; i < size; ++i) {
      std::string name;
      ar(name);

      std::unique_ptr<nf7::File> f;
      try {
        ar(f);
        items_[name] = std::move(f);
      } catch (nf7::Exception&) {
        env().Throw(std::current_exception());
      }
    }
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(opened_, win_);

    ar(static_cast<uint64_t>(items_.size()));
    for (auto& p : items_) {
      ar(p.first, p.second);
    }
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
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
    nf7::FileBase::Handle(ev);
    switch (ev.type) {
    case Event::kAdd:
      // force to show window if this is the root
      if (name() == "$") {
        win_.shown() = true;
      }
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

  std::unordered_set<std::string> opened_;


  // GUI popup
  class WidgetPopup final :
      public nf7::FileBase::Feature, private nf7::gui::Popup {
   public:
    WidgetPopup(Dir& owner) noexcept :
        nf7::gui::Popup("WidgetPopup"), owner_(&owner) {
    }

    void Open(nf7::File& f) noexcept {
      target_ = &f;
      nf7::gui::Popup::Open();
    }
    void Update() noexcept override;

   private:
    Dir*       owner_;
    nf7::File* target_ = nullptr;
  } widget_popup_;

  class AddPopup final :
      public nf7::FileBase::Feature, private nf7::gui::Popup {
   public:
    AddPopup(Dir& owner) noexcept :
        nf7::gui::Popup("AddPopup"),
        owner_(&owner),
        factory_(owner, [](auto& t) { return t.flags().contains("nf7::DirItem"); },
                 nf7::gui::FileFactory::kNameInput |
                 nf7::gui::FileFactory::kNameDupCheck) {
    }

    using nf7::gui::Popup::Open;
    void Update() noexcept override;

   private:
    Dir* owner_;
    nf7::gui::FileFactory factory_;
  } add_popup_;

  class RenamePopup final :
      public nf7::FileBase::Feature, private nf7::gui::Popup {
   public:
    RenamePopup(Dir& owner) noexcept :
        nf7::gui::Popup("RenamePopup"),
        owner_(&owner) {
    }

    void Open(std::string_view before) noexcept {
      before_ = before;
      after_  = "";
      nf7::gui::Popup::Open();
    }
    void Update() noexcept override;

   private:
    Dir* owner_;
    std::string before_;
    std::string after_;
  } rename_popup_;


  std::string GetUniqueName(std::string_view name) const noexcept {
    auto ret = std::string {name};
    while (Find(ret)) {
      ret += "_dup";
    }
    return ret;
  }
};

void Dir::Update() noexcept {
  nf7::FileBase::Update();

  const auto em = ImGui::GetFontSize();

  // update children
  for (const auto& item : items_) {
    ImGui::PushID(item.second.get());
    item.second->Update();
    ImGui::PopID();
  }

  // tree view window
  if (win_.shownInCurrentFrame()) {
    ImGui::SetNextWindowSize({8*em, 8*em}, ImGuiCond_FirstUseEver);
  }
  if (win_.Begin()) {
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

    if (ditem && (ditem->flags() & DirItem::kWidget)) {
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        widget_popup_.Open(file);
      }
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
      if (ditem && (ditem->flags() & DirItem::kWidget)) {
        if (ImGui::MenuItem("open widget")) {
          widget_popup_.Open(file);
        }
      }
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
        rename_popup_.Open(name);
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
        add_popup_.Open();
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
    add_popup_.Open();
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

void Dir::WidgetPopup::Update() noexcept {
  if (nf7::gui::Popup::Begin()) {
    if (auto item = target_->interface<nf7::DirItem>()) {
      ImGui::PushID(item);
      item->UpdateWidget();
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
}
void Dir::AddPopup::Update() noexcept {
  if (nf7::gui::Popup::Begin()) {
    ImGui::TextUnformatted("System/Dir: adding new file...");
    if (factory_.Update()) {
      ImGui::CloseCurrentPopup();

      auto& env  = owner_->env();
      auto  ctx  = std::make_shared<nf7::GenericContext>(*owner_, "adding new item");
      auto  task = [this, &env]() { owner_->Add(factory_.name(), factory_.Create(env)); };
      env.ExecMain(ctx, std::move(task));
    }
    ImGui::EndPopup();
  }
}
void Dir::RenamePopup::Update() noexcept {
  if (nf7::gui::Popup::Begin()) {
    ImGui::TextUnformatted("System/Dir: renaming an exsting item...");
    ImGui::InputText("before", &before_);

    bool submit = false;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputText("after", &after_, ImGuiInputTextFlags_EnterReturnsTrue)) {
      submit = true;
    }

    bool err = false;
    if (!Find(before_)) {
      ImGui::Bullet(); ImGui::TextUnformatted("before is invalid: missing target");
      err = true;
    }
    if (Find(after_)) {
      ImGui::Bullet(); ImGui::TextUnformatted("after is invalid: duplicated name");
      err = true;
    }
    try {
      Path::ValidateTerm(after_);
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
            before_.c_str(), after_.c_str(),
            owner_->abspath().Stringify().c_str());
      }
    }

    if (submit) {
      ImGui::CloseCurrentPopup();

      auto ctx  = std::make_shared<nf7::GenericContext>(*owner_, "renaming item");
      auto task = [this, before = std::move(before_), after = std::move(after_)]() {
        auto f = owner_->Remove(before);
        if (!f) throw Exception("missing target");
        owner_->Add(after, std::move(f));
      };
      owner_->env().ExecMain(ctx, std::move(task));
    }
    ImGui::EndPopup();
  }
}

}
}  // namespace nf7
