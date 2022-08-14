#include "common/file_holder.hh"

#include <imgui.h>


using namespace std::literals;

namespace nf7 {

FileHolder::FileHolder(nf7::File& owner, std::string_view id, const FileHolder* src) :
    owner_(&owner), id_(id) {
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
bool FileHolder::UpdateButton(bool small) const noexcept {
  const auto text = GetDisplayText();
  return small? ImGui::SmallButton(text.c_str()): ImGui::Button(text.c_str());
}
bool FileHolder::UpdateButtonWithLabel(const char* name) const noexcept {
  const bool ret = ImGui::Button(
      GetDisplayText().c_str(), {ImGui::CalcItemWidth(), 0});
  ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
  ImGui::TextUnformatted(name);
  return ret;
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

}  // namespace nf7
