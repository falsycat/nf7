#include <atomic>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/async_buffer.hh"
#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/file_holder.hh"
#include "common/future.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/generic_watcher.hh"
#include "common/gui_file.hh"
#include "common/life.hh"
#include "common/lock.hh"
#include "common/luajit.hh"
#include "common/luajit_obj.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_thread.hh"
#include "common/logger_ref.hh"
#include "common/ptr_selector.hh"
#include "common/task.hh"
#include "common/yas_nf7.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Obj final : public nf7::FileBase, public nf7::DirItem, public nf7::luajit::Obj {
 public:
  static inline const nf7::GenericTypeInfo<Obj> kType = {
    "LuaJIT/Obj", {"nf7::DirItem", "nf7::luajit::Obj"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted(
        "Compiles and runs LuaJIT script, and caches the object returned from the script.");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "implements nf7::luajit::Obj implementation");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "requires nf7::luajit::Queue implementation with name '_luajit' on upper dir");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "requires nf7::AsyncBuffer implementation to load LuaJIT script");
  }

  static constexpr size_t kMaxSize = 1024*1024*16;  /* = 16 MiB */

  class ExecTask;

  struct Data final {
    nf7::FileHolder::Tag src;
  };

  Obj(Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env, {&src_, &src_editor_}),
      nf7::DirItem(nf7::DirItem::kTooltip |
                   nf7::DirItem::kMenu |
                   nf7::DirItem::kWidget),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>()),
      src_(*this, "src"),
      src_editor_(src_, [](auto& t) { return t.flags().contains("nf7::AsyncBuffer"); }),
      mem_(std::move(data)) {
    mem_.data().src.SetTarget(src_);
    mem_.CommitAmend();

    src_.onChildMementoChange = [this]() { mem_.Commit(); };
    src_.onEmplace            = [this]() { mem_.Commit(); };

    mem_.onRestore = [this]() { DropCache(); Touch(); };
    mem_.onCommit  = [this]() { DropCache(); Touch(); };
  }

  Obj(Env& env, Deserializer& ar) noexcept : Obj(env) {
    ar(src_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(src_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Obj>(env, Data {mem_.data()});
  }

  void Handle(const Event&) noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateWidget() noexcept override;

  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Build() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::luajit::Obj, nf7::Memento>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Obj> life_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::optional<nf7::GenericWatcher> watcher_;
  std::shared_ptr<nf7::luajit::Ref>  cache_;

  nf7::Task<std::shared_ptr<nf7::luajit::Ref>>::Holder exec_;

  nf7::FileHolder            src_;
  nf7::gui::FileHolderEditor src_editor_;

  nf7::GenericMemento<Data> mem_;


  void DropCache() noexcept;
};

class Obj::ExecTask final : public nf7::Task<std::shared_ptr<nf7::luajit::Ref>> {
 public:
  ExecTask(Obj& target) noexcept :
      Task(target.env(), target.id()), target_(&target), log_(target.log_) {
  }

  size_t GetMemoryUsage() const noexcept override {
    return buf_size_;
  }

 private:
  Obj* const target_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::string chunkname_;
  std::atomic<size_t> buf_size_ = 0;
  std::vector<uint8_t> buf_;
  bool buf_consumed_ = false;


  nf7::Future<std::shared_ptr<nf7::luajit::Ref>>::Coro Proc() noexcept override {
    try {
      auto& srcf = target_->src_.GetFileOrThrow();
      chunkname_ = srcf.abspath().Stringify();

      const auto srcf_id = srcf.id();

      // acquire lock of source
      auto src     = srcf.interfaceOrThrow<nf7::AsyncBuffer>().self();
      auto srclock = co_await src->AcquireLock(false);

      // get size of source
      buf_size_ = co_await src->size();
      if (buf_size_ == 0) {
        throw nf7::Exception("source is empty");
      }
      if (buf_size_ > kMaxSize) {
        throw nf7::Exception("source is too huge");
      }

      // read source
      buf_.resize(buf_size_);
      const size_t read = co_await src->Read(0, buf_.data(), buf_size_);
      if (read != buf_size_) {
        throw nf7::Exception("failed to read all bytes from source");
      }

      // find luajit queue
      auto ljq = target_->
          ResolveUpwardOrThrow("_luajit").
          interfaceOrThrow<nf7::luajit::Queue>().self();

      // create promise handler for new luajit thread
      nf7::Future<int>::Promise lua_pro(self());
      auto handler = nf7::luajit::Thread::CreatePromiseHandler<int>(
          lua_pro, [&](auto L) {
            if (lua_gettop(L) != 1) {
              throw nf7::Exception("expected one object to be returned");
            }
            return luaL_ref(L, LUA_REGISTRYINDEX);
          });

      // start watcher on target_->watcher_
      try {
        auto& w = target_->watcher_;
        w.emplace(env());
        w->Watch(srcf_id);

        std::weak_ptr<Task> wself = self();
        w->AddHandler(Event::kUpdate, [t = target_, wself](auto&) {
          if (auto self = wself.lock()) {
            t->log_->Info("detected update of source file, aborts building");
            t->exec_ = {};
          } else if (t->cache_) {
            t->log_->Info("detected update of source file, drops cache automatically");
            t->cache_ = nullptr;
            t->Touch();
          }
        });
      } catch (Exception& e) {
        log_->Warn("watcher setup error: "+e.msg());
      }

      // queue task to trigger the thread
      auto th = std::make_shared<nf7::luajit::Thread>(self(), ljq, std::move(handler));
      th->Install(log_);
      ljq->Push(self(), [&](auto L) {
        try {
          auto thL = th->Init(L);
          Compile(thL);
          th->Resume(thL, 0);
        } catch (Exception&) {
          lua_pro.Throw(std::current_exception());
        }
      });

      // wait for end of execution and return built object's index
      const int idx = co_await lua_pro.future();

      // context for object cache
      auto ctx = std::make_shared<nf7::GenericContext>(env(), initiator(), "luajit object cache");

      // return the object and cache it
      target_->cache_ = std::make_shared<nf7::luajit::Ref>(ctx, ljq, idx);
      co_yield target_->cache_;

    } catch (Exception& e) {
      throw;
    }
  }

  void Compile(lua_State* L) {
    static const auto kReader = [](lua_State*, void* selfptr, size_t* size) -> const char* {
      auto self = reinterpret_cast<ExecTask*>(selfptr);
      if (std::exchange(self->buf_consumed_, true)) {
        *size = 0;
        return nullptr;
      } else {
        *size = self->buf_.size();
        return reinterpret_cast<const char*>(self->buf_.data());
      }
    };
    if (0 != lua_load(L, kReader, this, chunkname_.c_str())) {
      throw nf7::Exception(lua_tostring(L, -1));
    }
  }
};


nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Obj::Build() noexcept {
  if (auto exec = exec_.lock()) return exec->fu();
  if (cache_) {
    return std::shared_ptr<nf7::luajit::Ref>{cache_};
  }

  auto exec = std::make_shared<ExecTask>(*this);
  exec->Start();
  exec_ = {exec};
  return exec->fu();
}
void Obj::Handle(const Event& ev) noexcept {
  nf7::FileBase::Handle(ev);
  switch (ev.type) {
  case Event::kAdd:
    log_->SetUp(*this);
    break;
  case Event::kRemove:
    DropCache();
    log_->TearDown();
    break;
 
  default:
    break;
  }
}
void Obj::DropCache() noexcept {
  exec_    = {};
  cache_   = nullptr;
  watcher_ = std::nullopt;
}

void Obj::UpdateMenu() noexcept {
  if (ImGui::MenuItem("build")) {
    Build();
  }
  if (ImGui::MenuItem("drop cache", nullptr, nullptr, !!cache_)) {
    DropCache();
  }
}
void Obj::UpdateTooltip() noexcept {
  ImGui::Text("cache: %s", cache_? "ready": "nothing");
  ImGui::Text("src  :");
  ImGui::Indent();
  src_editor_.Tooltip();
  ImGui::Unindent();
}
void Obj::UpdateWidget() noexcept {
  ImGui::TextUnformatted("LuaJIT/Obj: config");
  src_editor_.ButtonWithLabel("src");
  ImGui::Spacing();
  src_editor_.ItemWidget("src");
}

}
}  // namespace nf7

