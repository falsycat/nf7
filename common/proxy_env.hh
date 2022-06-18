#pragma once

#include <utility>

#include "nf7.hh"


namespace nf7 {

class ProxyEnv : public nf7::Env {
 public:
  ProxyEnv(Env& parent, const std::filesystem::path& npath) noexcept :
      Env(npath), parent_(&parent) {
  }
  ProxyEnv(Env& parent) noexcept : ProxyEnv(parent, parent.npath()) {
  }

  File* GetFile(File::Id id) const noexcept override {
    return parent_->GetFile(id);
  }

  void ExecMain(const std::shared_ptr<Context>& ctx, Task&& task) noexcept override {
    parent_->ExecMain(ctx, std::move(task));
  }
  void ExecSub(const std::shared_ptr<Context>& ctx, Task&& task) noexcept override {
    parent_->ExecSub(ctx, std::move(task));
  }
  void ExecAsync(const std::shared_ptr<Context>& ctx, Task&& task) noexcept override {
    parent_->ExecAsync(ctx, std::move(task));
  }

  void Handle(const File::Event& ev) noexcept override {
    parent_->Handle(ev);
  }
  void Save() noexcept override {
    parent_->Save();
  }

 protected:
  File::Id AddFile(File& f) noexcept override {
    return parent_->AddFile(f);
  }
  void RemoveFile(File::Id id) noexcept override {
    parent_->RemoveFile(id);
  }

  void AddContext(Context& ctx) noexcept override {
    parent_->AddContext(ctx);
  }
  void RemoveContext(Context& ctx) noexcept override {
    parent_->RemoveContext(ctx);
  }

  void AddWatcher(File::Id id, Watcher& w) noexcept override {
    parent_->AddWatcher(id, w);
  }
  void RemoveWatcher(Watcher& w) noexcept override {
    parent_->RemoveWatcher(w);
  }

 private:
  Env* const parent_;
};

}  // namespace nf7
