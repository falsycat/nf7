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
#include "common/file_ref.hh"
#include "common/future.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/generic_watcher.hh"
#include "common/gui_dnd.hh"
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

class Obj final : public nf7::File,
    public nf7::DirItem,
    public nf7::luajit::Obj {
 public:
  static inline const GenericTypeInfo<Obj> kType = {"LuaJIT/Obj", {"DirItem",}};
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

  Obj(Env& env, Path&& path = {}) noexcept :
      File(kType, env),
      DirItem(DirItem::kTooltip | DirItem::kMenu | DirItem::kDragDropTarget),
      log_(std::make_shared<nf7::LoggerRef>()),
      src_(*this, std::move(path)) {
  }

  Obj(Env& env, Deserializer& ar) noexcept : Obj(env) {
    ar(src_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(src_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Obj>(env, Path(src_.path()));
  }

  void Handle(const Event&) noexcept override;
  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateDragDropTarget() noexcept override;

  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Build() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::luajit::Obj>(t).Select(this);
  }

 private:
  std::shared_ptr<nf7::LoggerRef> log_;

  std::optional<nf7::GenericWatcher> watcher_;
  std::shared_ptr<nf7::luajit::Ref>  cache_;

  nf7::Task<std::shared_ptr<nf7::luajit::Ref>>::Holder exec_;

  const char* popup_ = nullptr;

  // persistent params
  nf7::FileRef src_;


  void Reset() noexcept;
};

class Obj::ExecTask final : public nf7::Task<std::shared_ptr<nf7::luajit::Ref>> {
 public:
  ExecTask(Obj& target) noexcept :
      Task(target.env(), target.id()), target_(&target), log_(target_->log_) {
  }

  size_t GetMemoryUsage() const noexcept override {
    return buf_size_;
  }

 private:
  Obj* target_;
  std::shared_ptr<nf7::LoggerRef> log_;

  std::string chunkname_;
  std::atomic<size_t> buf_size_ = 0;
  std::vector<uint8_t> buf_;
  bool buf_consumed_ = false;


  nf7::Future<std::shared_ptr<nf7::luajit::Ref>>::Coro Proc() noexcept override {
    try {
      auto& srcf = *target_->src_;
      chunkname_ = srcf.abspath().Stringify();

      // acquire lock of source
      auto src     = srcf.interfaceOrThrow<nf7::AsyncBuffer>().self();
      auto srclock = co_await src->AcquireLock(false);
      log_->Trace("source file lock acquired");

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

      // create thread to compile lua script
      auto ljq = target_->
          ResolveUpwardOrThrow("_luajit").
          interfaceOrThrow<nf7::luajit::Queue>().self();
      nf7::Future<int>::Promise lua_pro(self());
      auto handler = nf7::luajit::Thread::CreatePromiseHandler<int>(
          lua_pro, [&](auto L) {
            if (lua_gettop(L) != 1) {
              throw nf7::Exception("expected one object to be returned");
            }
            if (auto str = lua_tostring(L, -1)) {
              log_->Info("got '"s+str+"'");
            } else {
              log_->Info("got ["s+lua_typename(L, lua_type(L, -1))+"]");
            }
            return luaL_ref(L, LUA_REGISTRYINDEX);
          });

      // setup watcher
      try {
        *target_->src_;  // check if the src is alive

        auto& w = target_->watcher_;
        w.emplace(env());
        w->Watch(srcf.id());

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
      log_->Trace("task finished");

      // context for object cache
      // TODO use specific Context type
      auto ctx = std::make_shared<nf7::GenericContext>(env(), initiator(), "luajit object cache");

      // return the object and cache it
      target_->cache_ = std::make_shared<nf7::luajit::Ref>(ctx, ljq, idx);
      co_yield target_->cache_;

    } catch (Exception& e) {
      log_->Error(e.msg());
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
  if (cache_) return std::shared_ptr<nf7::luajit::Ref>{cache_};

  auto exec = std::make_shared<ExecTask>(*this);
  exec->Start();
  exec_ = {exec};
  return exec->fu();
}
void Obj::Handle(const Event& ev) noexcept {
  switch (ev.type) {
  case Event::kAdd:
    log_->SetUp(*this);
    break;
  case Event::kRemove:
    exec_    = {};
    cache_   = nullptr;
    watcher_ = std::nullopt;
    log_->TearDown();
    break;
 
  default:
    break;
  }
}
void Obj::Reset() noexcept {
  exec_    = {};
  cache_   = nullptr;
  watcher_ = std::nullopt;
}

void Obj::Update() noexcept {
  if (const auto popup = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(popup);
  }
  if (ImGui::BeginPopup("ConfigPopup")) {
    static std::string path_str;
    ImGui::TextUnformatted("LuaJIT/Obj: config");
    if (ImGui::IsWindowAppearing()) {
      path_str = src_.path().Stringify();
    }

    const bool submit = ImGui::InputText(
        "path", &path_str, ImGuiInputTextFlags_EnterReturnsTrue);

    Path path;
    bool err = false;
    try {
      path = Path::Parse(path_str);
    } catch (Exception& e) {
      ImGui::Bullet(); ImGui::Text("invalid path: %s", e.msg().c_str());
      err = true;
    }
    try {
      ResolveOrThrow(path);
    } catch (File::NotFoundException&) {
      ImGui::Bullet(); ImGui::Text("(target seems to be missing)");
    }

    if (!err) {
      if (ImGui::Button("ok") || submit) {
        ImGui::CloseCurrentPopup();

        if (path != src_.path()) {
          auto task = [this, p = std::move(path)]() mutable {
            src_ = std::move(p);
            Reset();
          };
          auto ctx = std::make_shared<
              nf7::GenericContext>(*this, "changing source path");
          env().ExecMain(ctx, std::move(task));
        }
      }
    }
    ImGui::EndPopup();
  }
}
void Obj::UpdateMenu() noexcept {
  if (ImGui::MenuItem("config")) {
    popup_ = "ConfigPopup";
  }
  ImGui::Separator();
  if (ImGui::MenuItem("try build")) {
    Build();
  }
  if (ImGui::MenuItem("drop cache", nullptr, nullptr, !!cache_)) {
    Reset();
  }
}
void Obj::UpdateTooltip() noexcept {
  ImGui::Text("source: %s", src_.path().Stringify().c_str());
  ImGui::Text("cache : %d", cache_? cache_->index(): -1);
  ImGui::TextDisabled("drop a file here to set it as source");
}
void Obj::UpdateDragDropTarget() noexcept {
  if (auto p = gui::dnd::Accept<Path>(gui::dnd::kFilePath)) {
    src_ = std::move(*p);
  }
}

}
}  // namespace nf7

