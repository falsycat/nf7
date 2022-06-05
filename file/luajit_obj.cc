#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/async_buffer.hh"
#include "common/conditional_queue.hh"
#include "common/dir_item.hh"
#include "common/file_ref.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/lock.hh"
#include "common/logger_ref.hh"
#include "common/luajit_obj.hh"
#include "common/luajit_queue.hh"
#include "common/ptr_selector.hh"
#include "common/yas_nf7.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Obj final : public nf7::File,
    public nf7::DirItem,
    public nf7::luajit::Obj {
 public:
  static inline const GenericTypeInfo<Obj> kType = {"LuaJIT/Obj", {"DirItem",}};

  static constexpr size_t kMaxSize = 1024*1024*16;  /* = 16 MiB */

  class SrcWatcher;
  class ExecTask;

  Obj(Env& env, Path&& path = {}) noexcept :
      File(kType, env), DirItem(DirItem::kTooltip | DirItem::kMenu),
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

  std::shared_future<std::shared_ptr<nf7::luajit::Ref>> Build() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::luajit::Queue>(t).Select(this);
  }

 private:
  std::shared_ptr<nf7::LoggerRef>     log_;
  std::shared_ptr<nf7::luajit::Queue> ljq_;

  std::unique_ptr<SrcWatcher> srcwatcher_;
  std::shared_ptr<nf7::luajit::Ref> cache_;

  std::weak_ptr<ExecTask> exec_;

  const char* popup_ = nullptr;

  // persistent params
  nf7::FileRef src_;


  void Touch() noexcept {
    if (!id()) return;
    env().Handle({.id = id(), .type = Event::kUpdate});
  }

  void Reset() noexcept;
};

class Obj::SrcWatcher final : public nf7::Env::Watcher {
 public:
  SrcWatcher(Obj& owner, File::Id id) :
      Watcher(owner.env()), owner_(&owner) {
    if (owner.id() == id) throw Exception("self watch");
    if (id == 0) throw Exception("invalid id");
    Watch(id);
  }

  void Handle(const File::Event& ev) noexcept override {
    if (ev.type == File::Event::kUpdate) {
      owner_->log_->Info("detected update of source file, drops cache automatically");
      owner_->cache_ = nullptr;
      owner_->Touch();
    }
  }

 private:
  Obj* const owner_;
};

class Obj::ExecTask final : public nf7::Context, public std::enable_shared_from_this<ExecTask> {
 public:
  using Context::Context;

  ExecTask(Obj& target) :
      Context(target.env(), target.id()),
      target_(&target), log_(target_->log_), ljq_(target_->ljq_),
      fu_(pro_.get_future().share()),
      chunk_name_(target_->abspath().Stringify()),
      src_(&(*target.src_).interfaceOrThrow<nf7::AsyncBuffer>()),
      src_lock_(src_->AcquireLock(false)){
  }

  void Start() noexcept {
    Proc();
  }
  void Update() noexcept {
    while (cq_.PopAndExec());
  }
  void Abort() noexcept override {
    abort_ = true;
  }
  size_t GetMemoryUsage() const noexcept override {
    return buf_size_;
  }

  std::shared_future<std::shared_ptr<nf7::luajit::Ref>>& fu() noexcept { return fu_; }

 private:
  Obj* target_;
  bool abort_ = false;

  std::shared_ptr<nf7::LoggerRef>     log_;
  std::shared_ptr<nf7::luajit::Queue> ljq_;
  nf7::ConditionalQueue cq_;

  std::promise<std::shared_ptr<nf7::luajit::Ref>>       pro_;
  std::shared_future<std::shared_ptr<nf7::luajit::Ref>> fu_;

  std::string                chunk_name_;
  nf7::AsyncBuffer*          src_;
  std::shared_ptr<nf7::Lock> src_lock_;

  enum Step { kInitial, kSrcLock, kSrcSize, kSrcRead, kExec, kFinish };
  Step step_ = kInitial;

  std::atomic<size_t> buf_size_ = 0;
  std::vector<uint8_t> buf_;
  bool buf_consumed_ = false;

  int reg_idx_;


  void Error(std::string_view msg) noexcept {
    pro_.set_exception(std::make_exception_ptr<Exception>({msg}));
    log_->Error(msg);
  }

  void Proc(std::future<size_t>& fu) noexcept
  try {
    return Proc(fu.get());
  } catch (Exception& e) {
    log_->Error(e.msg());
    pro_.set_exception(std::current_exception());
    return;
  }
  void Proc(size_t param = 0, lua_State* L = nullptr) noexcept {
    if (abort_) {
      Error("task aborted");
      return;
    }

    switch (step_) {
    case kInitial:
      step_ = kSrcLock;
      cq_.Push(src_lock_, [self = shared_from_this()](auto) { self->Proc(); });
      return;

    case kSrcLock:
      if (!src_lock_->acquired()) {
        Error("failed to lock source file");
        return;
      }
      log_->Trace("source file lock acquired");
      step_ = kSrcSize;
      cq_.Push(src_->size(), [self = shared_from_this()](auto& v) { self->Proc(v); });
      return;

    case kSrcSize:
      if (src_lock_->cancelled()) {  // ensure src_ is alive
        Error("source is expired");
        return;
      }
      if (param == 0) {
        Error("source is empty");
        return;
      }
      if (param > kMaxSize) {
        Error("source is too huge");
        return;
      }
      log_->Trace("source file size is "+std::to_string(param)+" bytes");
      buf_size_ = param;
      buf_.resize(param);
      step_ = kSrcRead;
      cq_.Push(src_->Read(0, buf_.data(), param),
               [self = shared_from_this()](auto& v) { self->Proc(v); });
      return;

    case kSrcRead:
      if (buf_.size() != param) {
        Error("cannot read whole bytes");
        return;
      }
      log_->Trace("read "+std::to_string(buf_size_)+" bytes from source file");
      step_ = kExec;
      ljq_->Push(shared_from_this(), [self = shared_from_this()](auto L) { self->Proc(0, L); });
      return;

    case kExec:  // runs on LuaJIT thread
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
      if (0 != lua_load(L, kReader, this, chunk_name_.c_str())) {
        Error(lua_tostring(L, -1));
        return;
      }
      if (0 != lua_pcall(L, 0, 1, 0)) {
        Error(lua_tostring(L, -1));
        return;
      }
      log_->Trace("executed lua script and got "s+lua_typename(L, lua_type(L, -1)));
      reg_idx_ = luaL_ref(L, LUA_REGISTRYINDEX);
      if (reg_idx_ == LUA_REFNIL) {
        Error("got nil object");
        return;
      }
      step_ = kFinish;
      env().ExecSub(shared_from_this(),
                    [self = shared_from_this()]() { self->Proc(); });
      return;

    case kFinish:
      log_->Trace("task finished"s);
      {
        auto ctx = std::make_shared<nf7::GenericContext>(env(), initiator());
        ctx->description() = "luajit object cache";
        target_->cache_ = std::make_shared<nf7::luajit::Ref>(ctx, ljq_, reg_idx_);
      }
      pro_.set_value(target_->cache_);
      return;

    default:
      assert(false);
    }
  }
};


std::shared_future<std::shared_ptr<nf7::luajit::Ref>> Obj::Build() noexcept
try {
  if (!ljq_) throw Exception("luajit context not found");
  auto exec = exec_.lock();
  if (!exec) {
    if (cache_) {
      std::promise<std::shared_ptr<nf7::luajit::Ref>> pro;
      pro.set_value(cache_);
      return pro.get_future().share();
    }
    exec_ = exec = std::make_shared<ExecTask>(*this);
    exec->Start();
  }
  return exec->fu();
} catch (Exception& e) {
  log_->Error(e.msg());
  std::promise<std::shared_ptr<nf7::luajit::Ref>> pro;
  pro.set_exception(std::current_exception());
  return pro.get_future().share();
}
void Obj::Handle(const Event& ev) noexcept {
  switch (ev.type) {
  case Event::kAdd:
    try {
      log_->SetUp(*this);
      ljq_ = ResolveUpwardOrThrow("_luajit").
          interfaceOrThrow<nf7::luajit::Queue>().self();
      auto ctx = std::make_shared<nf7::GenericContext>(env(), id());
      ctx->description() = "resetting state";
      env().ExecMain(ctx, [this]() { Reset(); });
    } catch (Exception&) {
    }
    break;
  case Event::kRemove:
    if (auto exec = exec_.lock()) exec->Abort();
    exec_ = {};
    cache_      = nullptr;
    ljq_        = nullptr;
    srcwatcher_ = nullptr;
    log_->TearDown();
    break;
 
  default:
    break;
  }
}
void Obj::Reset() noexcept {
  if (auto exec = exec_.lock()) exec->Abort();
  exec_  = {};
  cache_ = nullptr;
  try {
    srcwatcher_ = std::make_unique<SrcWatcher>(*this, src_.id());
  } catch (Exception&) {
    srcwatcher_ = nullptr;
  }
}

void Obj::Update() noexcept {
  if (auto exec = exec_.lock()) exec->Update();

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
          auto ctx  = std::make_shared<nf7::GenericContext>(env(), id());
          auto task = [this, p = std::move(path)]() mutable {
            src_.path() = std::move(p);
            Reset();
          };
          ctx->description() = "changing source path";
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
    cache_ = nullptr;
  }
}
void Obj::UpdateTooltip() noexcept {
  ImGui::Text("source: %s", src_.path().Stringify().c_str());
  ImGui::Text("cache : %d", cache_? cache_->index(): -1);
}

}
}  // namespace nf7

