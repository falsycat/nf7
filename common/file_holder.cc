#include "common/file_holder.hh"

#include <optional>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "common/generic_context.hh"


using namespace std::literals;

namespace nf7 {

FileHolder::FileHolder(nf7::File& owner, std::string_view id, const FileHolder* src) :
    owner_(&owner), id_(id), popup_config_(*this) {
  if (src) {
    if (src->own()) {
      entity_ = src->file()->Clone(owner.env());
    } else {
      entity_ = src->entity_;
    }
    // SetUp() will be called by kAdd event
  }
}

nf7::File* FileHolder::Find(std::string_view name) const noexcept {
  return name == id_? file_: nullptr;
}
void FileHolder::Handle(const nf7::File::Event& ev) noexcept {
  switch (ev.type) {
  case nf7::File::Event::kAdd:
    assert(!ready_);
    ready_ = true;
    SetUp();
    break;
  case nf7::File::Event::kRemove:
    assert(ready_);
    TearDown();
    ready_ = false;
    break;
  default:
    break;
  }
}
void FileHolder::Update() noexcept {
  if (own()) {
    ImGui::PushID(this);
    file_->Update();
    ImGui::PopID();
  }
  popup_config_.Update();
}

std::string FileHolder::GetDisplayText() const noexcept {
  std::string text;
  if (own()) {
    text = "OWN: " + file_->type().name();
  } else if (empty()) {
    text = "NULL:";
  } else {
    text = "REF: "s + path().Stringify();
  }
  return text;
}
void FileHolder::UpdateButton(bool small) noexcept {
  ImGui::PushID(this);

  const auto text = GetDisplayText();
  if (small? ImGui::SmallButton(text.c_str()): ImGui::Button(text.c_str())) {
    popup_config_.Open();
  }

  ImGui::PopID();
}
void FileHolder::UpdateLabel(const char* name) noexcept {
  ImGui::PushID(this);

  if (ImGui::Button(GetDisplayText().c_str(), {ImGui::CalcItemWidth(), 0})) {
    popup_config_.Open();
  }
  ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
  ImGui::TextUnformatted(name);

  ImGui::PopID();
}

void FileHolder::SetUp() noexcept {
  if (!ready_ || file_) return;
  if (own()) {
    file_->MoveUnder(*owner_, id_);
  } else if (ref()) {
    try {
      file_ = &owner_->ResolveOrThrow(path());
    } catch (nf7::File::NotFoundException&) {
    }
  }
  if (file_) {
    watcher_.emplace(file_->env());
    watcher_->Watch(file_->id());

    watcher_->AddHandler(nf7::File::Event::kRemove, [this](auto&) {
      file_ = nullptr;
    });
    auto mem = file_->interface<nf7::Memento>();
    if (own() && mem) {
      watcher_->AddHandler(nf7::File::Event::kUpdate, [this, mem](auto&) {
        auto ptag = std::exchange(tag_, mem->Save());
        if (ptag != tag_) {
          onChange();
        }
      });
    }
  }
}
void FileHolder::TearDown() noexcept {
  if (!ready_) return;
  if (own()) {
    file_->Isolate();
  }
  file_    = nullptr;
  watcher_ = std::nullopt;
}


FileHolder::Tag::Tag(const Tag& src) noexcept {
  if (src.target_) {
    entity_ = src.target_->entity_;
    tag_    = src.target_->tag_;
  } else {
    entity_ = src.entity_;
    tag_    = src.tag_;
  }
}
FileHolder::Tag& FileHolder::Tag::operator=(const Tag& src) noexcept {
  assert(!src.target_);
  assert(target_);

  target_->TearDown();
  target_->entity_ = src.entity_;
  target_->tag_    = src.tag_;
  if (target_->tag_) {
    target_->file()->interfaceOrThrow<nf7::Memento>().Restore(target_->tag_);
  }
  target_->SetUp();

  return *this;
}


void FileHolder::ConfigPopup::Open() noexcept {
  if (h_->ref()) {
    type_ = 1;
    path_ = h_->path().Stringify();
  } else {
    type_ = 0;
    path_ = "";
  }
  nf7::gui::Popup::Open();
}
void FileHolder::ConfigPopup::Update() noexcept {
  ImGui::PushID(this);

  auto& owner = *h_->owner_;

  if (Begin()) {
    if (ImGui::RadioButton("own", type_ == 0)) { type_ = 0; }
    ImGui::SameLine();
    if (ImGui::RadioButton("ref", type_ == 1)) { type_ = 1; }

    if (type_ == 0) {
      if (factory_.Update()) {
        // TODO
      }

    } else if (type_ == 1) {
      ImGui::InputText("path", &path_);

      bool missing = false;
      try {
        auto path = nf7::File::Path::Parse(path_);
        try {
          owner.ResolveOrThrow(path);
        } catch (nf7::File::NotFoundException&) {
          missing = true;
        }

        if (ImGui::Button("ok")) {
          ImGui::CloseCurrentPopup();

          auto ctx = std::make_shared<nf7::GenericContext>(
              owner, "emplacing reference to file holder '"+h_->id_+"'");
          owner.env().ExecMain(
              ctx, [this, p = std::move(path)]() mutable {
                h_->Emplace(std::move(p));
                h_->onChange();
              });
        }
      } catch (nf7::Exception& e) {
        ImGui::Bullet(); ImGui::TextUnformatted(e.msg().c_str());
      }
      if (missing) {
        ImGui::Bullet(); ImGui::TextUnformatted("the file is missing :(");
      }
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();
}

}  // namespace nf7
