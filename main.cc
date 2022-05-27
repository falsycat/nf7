#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
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
#include "common/yas.hh"

// Include glfw lastly to prevent conflict with windows.h.
#include <GLFW/glfw3.h>


using namespace nf7;
using namespace std::literals;

class Env final : public nf7::Env {
 public:
  static constexpr auto kFileName = "root.nf7";
  static constexpr char kDefaultRoot[] = {
#   include "generated/root.nf7.inc"
  };

  static constexpr size_t kSubTaskUnit = 64;

  Env() noexcept : nf7::Env(std::filesystem::current_path()) {
    ::Env::Push(*this);

    // start threads
    main_thread_ = std::thread([this]() { MainThread(); });
    async_threads_.resize(std::max<size_t>(std::thread::hardware_concurrency(), 2));
    for (auto& th : async_threads_) {
      th = std::thread([this]() { AsyncThread(); });
    }

    // deserialize
    try {
      if (!std::filesystem::exists(kFileName)) {
        std::ofstream of(kFileName, std::ios::binary);
        if (!of) throw Exception("failed to open native file: "s+kFileName);
        of.write(kDefaultRoot, sizeof(kDefaultRoot));
        of.flush();
        if (!of) throw Exception("failed to write to native file: "s+kFileName);
      }
      try {
        yas::load<yas::file|yas::binary>("root.nf7", root_);
        root_->MakeAsRoot();
      } catch (yas::io_exception&) {
        throw Exception("failed to read: "s+kFileName);
      }
    } catch (Exception&) {
      Panic();
    }
  }
  ~Env() noexcept {
    alive_ = false;
    cv_.notify_one();
    async_.Notify();

    main_thread_.join();
    for (auto& th : async_threads_) th.join();

    if (root_) root_->Isolate();
    ::Env::Pop();
  }

  void ExecMain(const std::shared_ptr<Context>&, Task&& task) noexcept override {
    main_.Push(std::move(task));
  }
  void ExecSub(const std::shared_ptr<Context>&, Task&& task) noexcept override {
    sub_.Push(std::move(task));
  }
  void ExecAsync(const std::shared_ptr<Context>&, Task&& task) noexcept override {
    async_.Push(std::move(task));
  }

  void Handle(const File::Event& e) noexcept override
  try {
    // trigger File::Handle()
    GetFile(e.id).Handle(e);

    // trigger file watcher
    auto itr = watchers_map_.find(e.id);
    if (itr != watchers_map_.end()) {
      for (auto w : itr->second) w->Handle(e);
    }

    // trigger global watcher
    for (auto w : watchers_map_[0]) w->Handle(e);
  } catch (ExpiredException&) {
  }

  void Save() noexcept override {
    yas::file_ostream os(kFileName, yas::file_trunc);
    yas::binary_oarchive<yas::file_ostream, yas::binary> oa(os);
    oa & root_;
  }

  void Update() noexcept {
    interrupt_ = true;
    std::unique_lock<std::mutex> _(mtx_);

    ImGui::PushID(this);
    {
      if (root_) {
        ImGui::PushID(root_.get());
        root_->Update();
        ImGui::PopID();
      }
      UpdatePanic();
    }
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
    return itr->first;
  }
  void RemoveFile(File::Id id) noexcept override {
    files_.erase(id);
  }

  void AddContext(Context& ctx) noexcept override {
    ctxs_.push_back(&ctx);
  }
  void RemoveContext(Context& ctx) noexcept override {
    ctxs_.erase(std::remove(ctxs_.begin(), ctxs_.end(), &ctx), ctxs_.end());
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
  std::unique_ptr<File> root_;

  std::atomic<bool> alive_ = true;
  std::exception_ptr panic_;

  File::Id file_next_ = 1;
  std::unordered_map<File::Id, File*> files_;

  std::vector<Context*> ctxs_;

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
    const auto em = ImGui::GetFontSize();

    ImGui::SetNextWindowSize({16*em, 12*em}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("panic")) {
      ImGui::TextUnformatted("something went wrong X(");

      auto size = ImGui::GetContentRegionAvail();
      size.y -= ImGui::GetFrameHeightWithSpacing();

      const auto kFlags = ImGuiWindowFlags_HorizontalScrollbar;
      if (ImGui::BeginChild("panic_detail", size, true, kFlags)) {
        auto ptr = panic_;
        while (ptr)
        try {
          std::rethrow_exception(ptr);
        } catch (Exception& e) {
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
        // TODO: how to handle?
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
    env.Save();
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
