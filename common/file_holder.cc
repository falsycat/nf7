#include "common/file_holder.hh"

#include <imgui.h>


using namespace std::literals;

namespace nf7 {

nf7::File* FileHolder::Find(std::string_view name) const noexcept {
  return name == id_? file_: nullptr;
}
void FileHolder::Handle(const nf7::File::Event& ev) noexcept {
  switch (ev.type) {
  case nf7::File::Event::kAdd:
    SetUp();
    break;
  case nf7::File::Event::kRemove:
    TearDown();
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

void FileHolder::SetUp() noexcept {
  const bool first_setup = !file_;

  if (own()) {
    file_ = std::get<std::shared_ptr<nf7::File>>(entity_).get();
    if (owner_->id() && file_->id() == 0) {
      file_->MoveUnder(*owner_, id_);
    }

  } else if (ref()) {
    if (owner_->id()) {
      try {
        file_ = &owner_->ResolveOrThrow(path());
      } catch (nf7::File::NotFoundException&) {
        file_ = nullptr;
      }
    }
  }

  if (file_) {
    auto mem = own()? file_->interface<nf7::Memento>(): nullptr;

    // init watcher
    if (file_->id() && !watcher_) {
      watcher_.emplace(file_->env());
      watcher_->Watch(file_->id());

      watcher_->AddHandler(nf7::File::Event::kUpdate, [this, mem](auto&) {
        if (mem) {
          auto ptag = std::exchange(tag_, mem->Save());
          if (ptag != tag_) {
            onChildMementoChange();
            if (mem_) mem_->Commit();  // commit owner's memento
          }
        }
        onChildUpdate();
        owner_->Touch();
      });
    }

    // memento setup
    if (first_setup && mem) {
      if (!tag_) {
        tag_ = mem->Save();
      } else {
        mem->Restore(tag_);
      }
    }
  }
}
void FileHolder::TearDown() noexcept {
  if (!owner_->id()) return;
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
  if (!src.target_ && target_) {
    // restore
    target_->TearDown();
    target_->entity_ = src.entity_;
    target_->tag_    = src.tag_;
    target_->SetUp();
  } else if (!src.target_ && !target_) {
    // shallow copy
    entity_ = src.entity_;
    tag_    = src.tag_;
  } else {
    assert(false);
  }
  return *this;
}
void FileHolder::Tag::SetTarget(nf7::FileHolder& h) noexcept {
  assert(!target_);

  target_ = &h;

  h.TearDown();
  if (std::holds_alternative<nf7::File::Path>(entity_)) {
    h.Emplace(std::move(std::get<nf7::File::Path>(entity_)));
  } else if (std::holds_alternative<std::shared_ptr<nf7::File>>(entity_)) {
    h.Emplace(std::get<std::shared_ptr<nf7::File>>(entity_)->Clone(h.env()));
  }
  entity_ = std::monostate {};
  tag_    = nullptr;
  h.SetUp();
}

}  // namespace nf7
