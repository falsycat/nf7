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
      }
    }
  }

  if (file_) {
    auto mem = file_->interface<nf7::Memento>();

    // init watcher
    if (owner_->id() && !watcher_) {
      watcher_.emplace(file_->env());
      watcher_->Watch(file_->id());

      watcher_->AddHandler(nf7::File::Event::kRemove, [this](auto&) {
        file_ = nullptr;
      });
      if (mem) {
        watcher_->AddHandler(nf7::File::Event::kUpdate, [this, mem](auto&) {
          auto ptag = std::exchange(tag_, mem->Save());
          if (ptag != tag_) {
            onChildMementoChange();
          }
        });
      }
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
    // copy
    entity_ = src.entity_;
    tag_    = src.tag_;
  } else {
    assert(false);
  }
  return *this;
}

}  // namespace nf7
