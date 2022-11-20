#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <tracy/Tracy.hpp>

#include "nf7.hh"

#include "common/queue.hh"
#include "common/stopwatch.hh"
#include "common/timed_queue.hh"
#include "common/yas_nf7.hh"

// Include glfw lastly to prevent conflict with windows.h.
#include <GLFW/glfw3.h>

#include "init.hh"
#include "theme.hh"


using namespace std::literals;

namespace {

constexpr auto        kFrameDur   = 1000ms / 30;
constexpr auto        kSubTaskDur = 5ms;
constexpr const char* kFontPath   = "./nf7.ttf";


std::atomic<bool> alive_ = true;

enum CycleState {
  kSyncUpdate,  // -> kUpdate
  kUpdate,      // -> kSyncDraw or kSleep
  kSyncDraw,    // -> kDraw
  kDraw,        // -> kSleep
  kSleep,       // -> kSyncUpdate
};
std::atomic<CycleState> cycle_ = kUpdate;
std::condition_variable cycle_cv_;
std::mutex              cycle_mtx_;

std::shared_mutex task_mtx_;
using Task = std::pair<std::shared_ptr<nf7::Context>, nf7::Env::Task>;
nf7::Queue<Task>      mainq_;
nf7::TimedQueue<Task> subq_;
nf7::TimedQueue<Task> asyncq_;
nf7::TimedQueue<Task> glq_;
nf7::Queue<std::exception_ptr> panicq_;


void WorkerThread() noexcept {
  [[maybe_unused]] const char kThreadId[] = "SyncWorker";
  tracy::SetThreadName("SyncWorker");

  std::unique_lock<std::mutex> k {cycle_mtx_};
  while (alive_) {
    FrameMarkStart(kThreadId);

    cycle_cv_.wait(k, []() { return cycle_ != kUpdate; });
    k.unlock();

    for (;;) {
      std::shared_lock<std::shared_mutex> sk {task_mtx_};
      auto task = mainq_.Pop();
      if (!task) break;
      try {
        ZoneScopedNC("main task", tracy::Color::Orange);
        if (task->first) {
          const auto str = task->first->GetDescription();
          ZoneText(str.data(), str.size());
        }
        task->second();
      } catch (nf7::Exception&) {
        sk.unlock();
        panicq_.Push(std::current_exception());
      }
    }

    // exec sub tasks
    while (cycle_ != kSyncUpdate) {
      for (nf7::Stopwatch sw; sw.dur() < kSubTaskDur;) {
        std::shared_lock<std::shared_mutex> sk {task_mtx_};
        const auto task = subq_.Pop();
        if (!task) break;
        try {
          ZoneScopedNC("sub task", tracy::Color::Green);
          if (task->first) {
            const auto str = task->first->GetDescription();
            ZoneText(str.data(), str.size());
          }
          task->second();
        } catch (nf7::Exception&) {
          sk.unlock();
          panicq_.Push(std::current_exception());
        }
      }

      k.lock();
      cycle_cv_.wait(k, []() {
        return cycle_ == kSyncUpdate || !subq_.idle();
      });
      k.unlock();
    }

    // tell the main thread to start GUI update
    k.lock();
    cycle_ = kUpdate;
    cycle_cv_.notify_all();

    FrameMarkEnd(kThreadId);
  }
  TracyMessageL("SyncWorker exitting");
}

void AsyncThread() noexcept {
  [[maybe_unused]] const char kThreadId[] = "AsyncWorker";
  tracy::SetThreadName("AsyncWorker");

  std::unique_lock<std::mutex> k {cycle_mtx_};
  while (alive_) {
    FrameMarkStart(kThreadId);

    const auto until = asyncq_.next().value_or(nf7::Env::Time::max());
    cycle_cv_.wait_until(k, until, []() { return !alive_ || !asyncq_.idle(); });
    k.unlock();

    for (;;) {
      std::shared_lock<std::shared_mutex> sk {task_mtx_};
      auto task = asyncq_.Pop();
      if (!task) break;
      try {
        ZoneScopedNC("async task", tracy::Color::Blue);
        if (task->first) {
          const auto str = task->first->GetDescription();
          ZoneText(str.data(), str.size());
        }
        task->second();
      } catch (nf7::Exception&) {
        sk.unlock();
        panicq_.Push(std::current_exception());
      }
    }
    k.lock();

    FrameMarkEnd(kThreadId);
  }
  TracyMessageL("AsyncWorker exitting");
}

void GLThread(GLFWwindow* window) noexcept {
  [[maybe_unused]] const char kThreadId[] = "GLWorker";
  tracy::SetThreadName("GLWorker");

  std::unique_lock<std::mutex> k {cycle_mtx_};

  // does nothing when the first cycle because the main thread is using GL context
  cycle_cv_.wait(k, []() { return cycle_ == kSyncDraw; });
  cycle_ = kDraw;
  cycle_cv_.notify_all();

  while (alive_) {
    FrameMarkStart(kThreadId);

    // wait for the end of GUI drawing
    cycle_cv_.wait(k, []() { return cycle_ != kDraw; });
    k.unlock();

    glfwMakeContextCurrent(window);
    for (nf7::Stopwatch sw; sw.dur() < kSubTaskDur;) {
      std::shared_lock<std::shared_mutex> sk {task_mtx_};
      auto task = glq_.Pop();
      if (!task) break;
      try {
        ZoneScopedNC("GL task", tracy::Color::Aqua);
        if (task->first) {
          const auto str = task->first->GetDescription();
          ZoneText(str.data(), str.size());
        }
        task->second();
      } catch (nf7::Exception&) {
        sk.unlock();
        panicq_.Push(std::current_exception());
      }
      assert(0 == glGetError());
    }
    glfwMakeContextCurrent(nullptr);

    k.lock();
    cycle_cv_.wait(k, []() { return cycle_ == kSyncDraw || !glq_.idle(); });
    if (cycle_ == kSyncDraw) {
      // tell the main thread to start GUI drawing
      cycle_ = kDraw;
      cycle_cv_.notify_all();
    }

    FrameMarkEnd(kThreadId);
  }
  TracyMessageL("GLWorker exitting");
}


class Env final : public nf7::Env {
 public:
  static constexpr auto kFileName = "root.nf7";

  Env() noexcept : nf7::Env(std::filesystem::current_path()) {
    ZoneScopedN("nf7::Env constructor");

    // deserialize
    if (!std::filesystem::exists(kFileName)) {
      root_ = CreateRoot(*this);
      root_->MakeAsRoot();
    } else {
      try {
        nf7::Deserializer::Load(*this, kFileName, root_);
        root_->MakeAsRoot();
      } catch (nf7::Exception&) {
        panicq_.Push(std::current_exception());
      }
    }
  }
  ~Env() noexcept {
    if (ctxs_.size() > 0) {
      std::cout << "context leak detected: " << ctxs_.size() << std::endl;
    }
  }

  void TearDownRoot() noexcept {
    ZoneScoped;
    if (root_) {
      Save();
      root_->Isolate();
      root_ = nullptr;
    }
  }

  void Exec(Executor                             type,
            const std::shared_ptr<nf7::Context>& ctx,
            Task&&                               task,
            Time                                 time) noexcept override {
    bool notify = false;
    switch (type) {
    case kMain:
      TracyMessageL("queue main task");
      mainq_.Push({ctx, std::move(task)});
      break;
    case kSub:
      TracyMessageL("queue sub task");
      subq_.Push(time, {ctx, std::move(task)});
      notify = true;
      break;
    case kAsync:
      TracyMessageL("queue async task");
      asyncq_.Push(time, {ctx, std::move(task)});
      notify = true;
      break;
    case kGL:
      TracyMessageL("queue gl task");
      glq_.Push(time, {ctx, std::move(task)});
      notify = true;
      break;
    }
    if (notify) {
      std::unique_lock<std::mutex> k {cycle_mtx_};
      cycle_cv_.notify_all();
    }
  }

  nf7::File* Handle(const nf7::File::Event& e) noexcept override
  try {
    // trigger File::Handle()
    auto& f = GetFileOrThrow(e.id);
    f.Handle(e);

    // trigger file watcher
    auto itr = watchers_.find(e.id);
    if (itr != watchers_.end()) {
      for (auto w : itr->second) w->Handle(e);
    }

    // trigger global watcher
    itr = watchers_.find(0);
    if (itr != watchers_.end()) {
      for (auto w : itr->second) w->Handle(e);
    }
    return &f;
  } catch (nf7::ExpiredException&) {
    return nullptr;
  }

  void Exit() noexcept override {
    TracyMessageL("exit requested");
    exit_requested_ = true;
  }
  void Save() noexcept override
  try {
    ZoneScoped;
    nf7::Serializer::Save(*this, kFileName, root_);
  } catch (nf7::Exception&) {
    panicq_.Push(std::current_exception());
  }
  void Throw(std::exception_ptr&& ptr) noexcept override {
    panicq_.Push(std::move(ptr));
  }

  void Update() noexcept {
    ZoneScoped;
    ImGui::PushID(this);
    {
      if (root_) {
        ImGui::PushID(root_.get());
        root_->Update();
        ImGui::PopID();
      }
    }
    ImGui::PopID();
  }

  bool exitRequested() const noexcept { return exit_requested_; }

 protected:
  nf7::File* GetFile(nf7::File::Id id) const noexcept override {
    auto itr = files_.find(id);
    return itr != files_.end()? itr->second: nullptr;
  }
  nf7::File::Id AddFile(nf7::File& f) noexcept override {
    auto [itr, ok] = files_.emplace(file_next_++, &f);
    assert(ok);
    return itr->first;
  }
  void RemoveFile(nf7::File::Id id) noexcept override {
    files_.erase(id);
  }

  void AddContext(nf7::Context& ctx) noexcept override {
    std::unique_lock<std::mutex> k {ctx_mtx_};
    ctxs_.insert(&ctx);
  }
  void RemoveContext(nf7::Context& ctx) noexcept override {
    std::unique_lock<std::mutex> k {ctx_mtx_};
    ctxs_.erase(&ctx);
  }

  void AddWatcher(nf7::File::Id id, nf7::Env::Watcher& w) noexcept override {
    watchers_[id].push_back(&w);
  }
  void RemoveWatcher(nf7::File::Id id, nf7::Env::Watcher& w) noexcept override {
    auto itr = watchers_.find(id);
    if (watchers_.end() != itr) {
      auto& v = itr->second;
      v.erase(std::remove(v.begin(), v.end(), &w), v.end());
      if (v.size() == 0) {
        watchers_.erase(itr);
      }
    }
  }

 private:
  std::atomic<bool> exit_requested_ = false;

  std::unique_ptr<nf7::File> root_;

  nf7::File::Id file_next_ = 1;
  std::unordered_map<nf7::File::Id, nf7::File*> files_;

  std::unordered_map<nf7::File::Id, std::vector<nf7::Env::Watcher*>> watchers_;

  std::mutex ctx_mtx_;
  std::unordered_set<nf7::Context*> ctxs_;
};


void UpdatePanic() noexcept {
  ZoneScoped;

  static std::exception_ptr ptr_;
  if (!ptr_) {
    if (auto ptr = panicq_.Pop()) {
      ptr_ = *ptr;
    }
  }

  const auto em = ImGui::GetFontSize();
  ImGui::SetNextWindowSize({32*em, 24*em}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("panic")) {
    ImGui::TextUnformatted("something went wrong X(");

    auto size = ImGui::GetContentRegionAvail();
    size.y -= ImGui::GetFrameHeightWithSpacing();

    const auto kFlags = ImGuiWindowFlags_HorizontalScrollbar;
    if (ImGui::BeginChild("panic_detail", size, true, kFlags)) {
      auto ptr = ptr_;
      while (ptr)
      try {
        std::rethrow_exception(ptr);
      } catch (nf7::Exception& e) {
        e.UpdatePanic();
        ImGui::Separator();
        ptr = e.reason();
      } catch (std::exception& e) {
        ImGui::Text("std::exception (%s)", e.what());
        ImGui::Separator();
        ptr = nullptr;
      }
      ImGui::TextUnformatted("====END OF STACK====");
    }
    ImGui::EndChild();

    if (ImGui::Button("abort")) {
      std::abort();
    }
    ImGui::SameLine();
    if (ImGui::Button("ignore")) {
      ptr_ = {};
      ImGui::CloseCurrentPopup();
    }
    if (const auto rem = panicq_.size()) {
      ImGui::SameLine();
      ImGui::Text("other %zu exceptions are also causing panic", rem);
    }
    ImGui::EndPopup();
  } else {
    if (ptr_) ImGui::OpenPopup("panic");
  }
}

}  // namespace


int main(int, char**) {
  // init GLFW
  glfwSetErrorCallback(
      [](int, const char* msg) {
        std::cout << "GLFW error: " << msg << std::endl;
      });
  if (!glfwInit()) return 1;

  // create window
  GLFWwindow* window;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  window = glfwCreateWindow(1280, 720, "Nf7", NULL, NULL);
  if (window == NULL) return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(0);
  if (glewInit() != GLEW_OK) return 1;

  // start threads
  auto th_worker = std::thread {WorkerThread};

  const auto cores = std::thread::hardware_concurrency();
  std::vector<std::thread> th_async(cores > 4? cores-3: 1);
  for (auto& th : th_async) th = std::thread {AsyncThread};

  auto th_gl = std::thread {[window]() { GLThread(window); }};

  // init ImGUI
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  auto& io = ImGui::GetIO();
  io.IniFilename  = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  SetUpImGuiStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  // load GUI font
  if (std::filesystem::exists(kFontPath)) {
    ZoneScopedN("load GUI font");
    io.Fonts->AddFontFromFileTTF(
        kFontPath, 16.f, nullptr, io.Fonts->GetGlyphRangesJapanese());
  }

  // main loop
  ::Env env;
  glfwShowWindow(window);
  while (!glfwWindowShouldClose(window) && !env.exitRequested()) {
    nf7::Stopwatch sw;

    {
      ZoneScopedN("handle events");
      glfwPollEvents();
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
    }

    // wait for sync thrad
    {
      cycle_ = kSyncUpdate;
      std::unique_lock<std::mutex> k {cycle_mtx_};
      cycle_cv_.notify_all();
      cycle_cv_.wait(k, []() { return cycle_ == kUpdate; });
    }

    {
      ZoneScopedN("update GUI");
      assert(cycle_ == kUpdate);
      env.Update();
      UpdatePanic();
      ImGui::Render();
    }

    // wait for GL thread
    {
      cycle_ = kSyncDraw;
      std::unique_lock<std::mutex> k {cycle_mtx_};
      cycle_cv_.notify_all();
      cycle_cv_.wait(k, []() { return cycle_ == kDraw; });
    }

    // GUI draw (OpenGL calls occur)
    {
      ZoneScopedN("update display");
      assert(cycle_ == kDraw);
      glfwMakeContextCurrent(window);
      int w, h;
      glfwGetFramebufferSize(window, &w, &h);
      glViewport(0, 0, w, h);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      glfwSwapBuffers(window);
      glfwMakeContextCurrent(nullptr);
    }

    // sleep
    {
      cycle_ = kSleep;
      std::unique_lock<std::mutex> k {cycle_mtx_};
      cycle_cv_.notify_all();
    }
    std::this_thread::sleep_for(kFrameDur - sw.dur());

    FrameMark;
  }

  // sync with worker thread and tear down filesystem
  {
    cycle_ = kSyncUpdate;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
    cycle_cv_.wait(k, []() { return cycle_ == kUpdate; });
  }

  {
    ZoneScopedN("teardown root");
    assert(cycle_ == kUpdate);
    env.TearDownRoot();
  }

  // notify other threads that the destruction is done
  {
    cycle_ = kSleep;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
  }

  {
    ZoneScopedN("wait for all tasks");
    for (;;) {
      {
        ZoneScopedN("check remained tasks");
        std::unique_lock<std::shared_mutex> sk {task_mtx_};
        if (!mainq_.size() && !subq_.size() && !asyncq_.size() && !glq_.size()) {
          break;
        }
      }
      std::this_thread::sleep_for(30ms);
    }
  }

  // request SyncWorker and AsyncWorker to exit
  {
    alive_ = false;
    cycle_ = kSyncUpdate;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
  }
  for (auto& th : th_async) th.join();
  th_worker.join();
  TracyMessageL("SyncWorker and AsyncWorker exited");

  // wake up GLWorker to exit
  {
    cycle_ = kSyncDraw;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
  }
  th_gl.join();
  TracyMessageL("GLWorker exited");

  {
    ZoneScopedN("tear down everything");

    // tear down ImGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    // tear down display
    glfwDestroyWindow(window);
    glfwTerminate();
  }
  return 0;
}


void* operator new(size_t n) {
  auto ptr = std::malloc(n);
  if (!ptr) {
    throw nf7::Exception {"allocation failure"};
  }
  TracyAlloc(ptr, n);
  return ptr;
}
void operator delete(void* ptr) noexcept {
  TracyFree(ptr);
  std::free(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
  operator delete(ptr);
}
