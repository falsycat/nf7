#include <algorithm>
#include <cassert>
#include <filesystem>
#include <thread>
#include <unordered_map>
#include <vector>

#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include "nf7.hh"

#include "common/queue.hh"

#include <GLFW/glfw3.h>


using namespace nf7;

class Env final : public nf7::Env {
 public:
  static constexpr size_t kSubTaskUnit = 64;

  Env() noexcept : nf7::Env(std::filesystem::current_path()) {
    // start threads
    main_thread_ = std::thread([this]() { MainThread(); });
    async_threads_.resize(std::max<size_t>(std::thread::hardware_concurrency(), 2));
    for (auto& th : async_threads_) {
      th = std::thread([this]() { AsyncThread(); });
    }
  }
  ~Env() noexcept {
    alive_ = false;
    cv_.notify_one();
    async_.Notify();

    main_thread_.join();
    for (auto& th : async_threads_) th.join();
  }

  void ExecMain(Context::Id, Task&& task) noexcept override {
    main_.Push(std::move(task));
  }
  void ExecSub(Context::Id, Task&& task) noexcept override {
    sub_.Push(std::move(task));
  }
  void ExecAsync(Context::Id, Task&& task) noexcept override {
    async_.Push(std::move(task));
  }

  void Update() noexcept {
    interrupt_ = true;
    std::unique_lock<std::mutex> _(mtx_);

    ImGui::PushID(this);
    UpdatePanic();
    ImGui::PopID();

    cv_.notify_one();
  }

 protected:
  File& GetFile(File::Id id) const override {
    auto itr = files_.find(id);
    if (itr == files_.end()) {
      throw ExpiredException("file ("+std::to_string(id)+") is expired");
    }
    return *itr->second;
  }
  File::Id AddFile(File& f) noexcept override {
    auto [itr, ok] = files_.emplace(file_next_++, &f);
    assert(ok);
    HandleEvent({ .id = itr->first, .type = File::Event::kCreate });
    return itr->first;
  }
  void RemoveFile(File::Id id) noexcept override {
    HandleEvent({ .id = id, .type = File::Event::kRemove });
    files_.erase(id);
  }
  void HandleEvent(const File::Event& e) noexcept override {
    for (auto w : watchers_map_[0]) w->Handle(e);

    auto itr = watchers_map_.find(e.id);
    if (itr == watchers_map_.end()) return;
    for (auto w : itr->second) w->Handle(e);
  }

  Context& GetContext(Context::Id id) const override {
    auto itr = ctxs_.find(id);
    if (itr == ctxs_.end()) {
      throw ExpiredException("context ("+std::to_string(id)+") is expired");
    }
    return *itr->second;
  }
  Context::Id AddContext(Context& ctx) noexcept override {
    auto [itr, ok] = ctxs_.emplace(ctx_next_++, &ctx);
    assert(ok);
    return itr->first;
  }
  void RemoveContext(Context::Id id) noexcept override {
    ctxs_.erase(id);
  }

  void AddWatcher(File::Id id, Watcher& w) noexcept override {
    watchers_map_[id].push_back(&w);
    watchers_rmap_[&w].push_back(id);
  }
  void RemoveWatcher(Watcher& w) noexcept override {
    for (const auto id : watchers_rmap_[&w]) {
      auto& v = watchers_map_[id];
      v.erase(std::remove(v.begin(), v.end(), &w), v.end());
    }
    watchers_rmap_.erase(&w);
  }

 private:
  std::atomic<bool> alive_ = true;
  std::exception_ptr panic_;

  File::Id file_next_ = 1;
  Context::Id ctx_next_ = 1;
  std::unordered_map<File::Id, File*> files_;
  std::unordered_map<Context::Id, Context*> ctxs_;

  std::unordered_map<File::Id, std::vector<Watcher*>> watchers_map_;
  std::unordered_map<Watcher*, std::vector<File::Id>> watchers_rmap_;

  Queue<Task>     main_;
  Queue<Task>     sub_;
  WaitQueue<Task> async_;

  std::mutex               mtx_;
  std::condition_variable  cv_;
  std::atomic<bool>        interrupt_;
  std::thread              main_thread_;
  std::vector<std::thread> async_threads_;


  void Panic(std::exception_ptr ptr = std::current_exception()) noexcept {
    panic_ = ptr;
  }
  void UpdatePanic() noexcept {
    if (ImGui::BeginPopupModal("panic")) {
      ImGui::TextUnformatted("something went wrong X(");

      ImGui::BeginGroup();
      {
        auto ptr = panic_;
        while (ptr)
        try {
          std::rethrow_exception(ptr);
        } catch (Exception& e) {
          e.UpdatePanic();
          ImGui::Separator();
        }
      }
      ImGui::EndGroup();

      if (ImGui::Button("abort")) {
        std::abort();
      }
      ImGui::SameLine();
      if (ImGui::Button("ignore")) {
        panic_ = {};
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    } else {
      if (panic_) ImGui::OpenPopup("panic");
    }
  }


  void MainThread() noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    while (alive_) {
      // exec main tasks
      while (auto task = main_.Pop())
      try {
        (*task)();
      } catch (Exception&) {
        Panic();
      }

      // exec sub tasks until interrupted
      while (!interrupt_) {
        for (size_t i = 0; i < kSubTaskUnit; ++i) {
          const auto task = sub_.Pop();
          if (!task) break;
          try {
            (*task)();
          } catch (Exception&) {
            Panic();
          }
        }
      }
      cv_.wait(k);
    }
  }
  void AsyncThread() noexcept {
    while (alive_) {
      while (auto task = async_.Pop())
      try {
        (*task)();
      } catch (Exception&) {
        Panic();
      }
      async_.Wait();
    }
  }
};

int main(int, char**) {
  // init display
  glfwSetErrorCallback(
      [](int, const char* msg) {
        std::cout << "GLFW error: " << msg << std::endl;
      });
  if (!glfwInit()) return 1;

  GLFWwindow* window;
  const char* glsl_version;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
# if defined(__APPLE__)
    glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
# else
    glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
# endif
  window = glfwCreateWindow(1280, 720, "Nf7", NULL, NULL);
  if (window == NULL) return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  if (glewInit() != GLEW_OK) return 1;

  // init ImGUI
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  auto& io = ImGui::GetIO();
  io.IniFilename  = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // main logic
  {
    ::Env env;
    glfwShowWindow(window);
    while (!glfwWindowShouldClose(window)) {
      // new frame
      glfwPollEvents();
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      env.Update();

      // render windows
      ImGui::Render();
      int w, h;
      glfwGetFramebufferSize(window, &w, &h);
      glViewport(0, 0, w, h);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      glfwSwapBuffers(window);
    }
  }

  // teardown ImGUI
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  // teardown display
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
