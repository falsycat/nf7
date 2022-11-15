#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
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

#include "nf7.hh"

#include "common/queue.hh"
#include "common/timed_queue.hh"
#include "common/yas_nf7.hh"

// Include glfw lastly to prevent conflict with windows.h.
#include <GLFW/glfw3.h>

#include "init.hh"
#include "theme.hh"


using namespace std::literals;

namespace {

constexpr size_t      kSubTaskUnit = 64;
constexpr const char* kFontPath    = "./nf7.ttf";


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
nf7::Queue<Task>      subq_;
nf7::TimedQueue<Task> asyncq_;
nf7::TimedQueue<Task> glq_;
nf7::Queue<std::exception_ptr> panicq_;


void WorkerThread() noexcept {
  std::unique_lock<std::mutex> k {cycle_mtx_};
  while (alive_) {
    // wait for the end of GUI update
    cycle_cv_.wait(k, []() { return cycle_ != kUpdate; });
    k.unlock();

    // exec main tasks
    for (;;) {
      std::shared_lock<std::shared_mutex> sk {task_mtx_};
      auto task = mainq_.Pop();
      if (!task) break;
      try {
        task->second();
      } catch (nf7::Exception&) {
        sk.unlock();
        panicq_.Push(std::current_exception());
      }
    }

    // exec sub tasks
    while (cycle_ != kSyncUpdate) {
      for (size_t i = 0; i < kSubTaskUnit; ++i) {
        std::shared_lock<std::shared_mutex> sk {task_mtx_};
        const auto task = subq_.Pop();
        if (!task) break;
        try {
          task->second();
        } catch (nf7::Exception&) {
          sk.unlock();
          panicq_.Push(std::current_exception());
        }
      }

      k.lock();
      cycle_cv_.wait(k, []() {
        return cycle_ == kSyncUpdate || subq_.size() > 0;
      });
      k.unlock();
    }

    // tell the main thread to start GUI update
    k.lock();
    cycle_ = kUpdate;
    cycle_cv_.notify_all();
  }
}

void AsyncThread() noexcept {
  std::unique_lock<std::mutex> k {cycle_mtx_};
  while (alive_) {
    const auto until = asyncq_.next().value_or(nf7::Env::Time::max());
    cycle_cv_.wait_until(k, until, []() { return !alive_ || !asyncq_.idle(); });
    k.unlock();

    for (;;) {
      std::shared_lock<std::shared_mutex> sk {task_mtx_};
      auto task = asyncq_.Pop();
      if (!task) break;
      try {
        task->second();
      } catch (nf7::Exception&) {
        sk.unlock();
        panicq_.Push(std::current_exception());
      }
    }
    k.lock();
  }
}

void GLThread(GLFWwindow* window) noexcept {
  std::unique_lock<std::mutex> k {cycle_mtx_};

  // does nothing when the first cycle because the main thread is using GL context
  cycle_cv_.wait(k, []() { return cycle_ == kSyncDraw; });
  cycle_ = kDraw;
  cycle_cv_.notify_all();

  while (alive_) {
    // wait for the end of GUI drawing
    cycle_cv_.wait(k, []() { return cycle_ != kDraw; });
    k.unlock();

    glfwMakeContextCurrent(window);
    for (size_t i = 0; i < kSubTaskUnit; ++i) {
      std::shared_lock<std::shared_mutex> sk {task_mtx_};
      auto task = glq_.Pop();
      if (!task) break;
      try {
        task->second();
      } catch (nf7::Exception&) {
        sk.unlock();
        panicq_.Push(std::current_exception());
      }
      assert(0 == glGetError());
    }
    glfwMakeContextCurrent(nullptr);

    k.lock();
    cycle_cv_.wait(k, []() { return cycle_ != kDraw || !glq_.idle(); });
    if (cycle_ == kSyncDraw) {
      // tell the main thread to start GUI drawing
      cycle_ = kDraw;
      cycle_cv_.notify_all();
    }
  }
}


class Env final : public nf7::Env {
 public:
  static constexpr auto kFileName = "root.nf7";

  Env() noexcept : nf7::Env(std::filesystem::current_path()) {
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
      mainq_.Push({ctx, std::move(task)});
      break;
    case kSub:
      subq_.Push({ctx, std::move(task)});
      notify = true;
      break;
    case kAsync:
      asyncq_.Push(time, {ctx, std::move(task)});
      notify = true;
      break;
    case kGL:
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
    exit_requested_ = true;
  }
  void Save() noexcept override
  try {
    nf7::Serializer::Save(*this, kFileName, root_);
  } catch (nf7::Exception&) {
    panicq_.Push(std::current_exception());
  }
  void Throw(std::exception_ptr&& ptr) noexcept override {
    panicq_.Push(std::move(ptr));
  }

  void Update() noexcept {
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
  glfwSwapInterval(1);
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
    io.Fonts->AddFontFromFileTTF(
        kFontPath, 16.f, nullptr, io.Fonts->GetGlyphRangesJapanese());
  }

  // main loop
  ::Env env;
  glfwShowWindow(window);
  while (!glfwWindowShouldClose(window) && !env.exitRequested()) {
    // handle events
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // sync with worker thread
    {
      cycle_ = kSyncUpdate;
      std::unique_lock<std::mutex> k {cycle_mtx_};
      cycle_cv_.notify_all();
      cycle_cv_.wait(k, []() { return cycle_ == kUpdate; });
    }

    // GUI update (OpenGL call is forbidden)
    assert(cycle_ == kUpdate);
    env.Update();
    UpdatePanic();
    ImGui::Render();

    // sync with GL thread
    {
      cycle_ = kSyncDraw;
      std::unique_lock<std::mutex> k {cycle_mtx_};
      cycle_cv_.notify_all();
      cycle_cv_.wait(k, []() { return cycle_ == kDraw; });
    }

    // GUI draw (OpenGL calls occur)
    assert(cycle_ == kDraw);
    glfwMakeContextCurrent(window);
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
    glfwMakeContextCurrent(nullptr);

    // sleep
    {
      cycle_ = kSleep;
      std::unique_lock<std::mutex> k {cycle_mtx_};
      cycle_cv_.notify_all();
    }
    std::this_thread::sleep_for(10ms);
  }

  // sync with worker thread and tear down filesystem
  {
    cycle_ = kSyncUpdate;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
    cycle_cv_.wait(k, []() { return cycle_ == kUpdate; });
  }
  assert(cycle_ == kUpdate);
  env.TearDownRoot();

  // notify other threads that the destruction is done
  {
    cycle_ = kSleep;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
  }

  // wait for all tasks
  for (;;) {
    std::unique_lock<std::shared_mutex> sk {task_mtx_};
    if (!mainq_.size() && !subq_.size() && !asyncq_.size() && !glq_.size()) {
      break;
    }
    std::this_thread::sleep_for(30ms);
  }

  // exit worker and async threads
  {
    alive_ = false;
    cycle_ = kSyncUpdate;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
  }
  for (auto& th : th_async) th.join();
  th_worker.join();

  // exit GL thread
  {
    cycle_ = kSyncDraw;
    std::unique_lock<std::mutex> k {cycle_mtx_};
    cycle_cv_.notify_all();
  }
  th_gl.join();

  // tear down ImGUI
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  // tear down display
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
