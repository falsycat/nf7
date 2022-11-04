#include <atomic>
#include <typeinfo>
#include <memory>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <imgui.h>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/font_queue.hh"
#include "common/generic_type_info.hh"
#include "common/ptr_selector.hh"
#include "common/thread.hh"


namespace nf7 {
namespace {

class FontContext final : public nf7::File, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<FontContext> kType = {
    "Font/Context", {"nf7::DirItem",}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Drives freetype context.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::font::Queue");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "there's few merit to use multiple contexts");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "the context remains alive after file deletion until unused");
  }

  class Queue;

  FontContext(nf7::Env& env) noexcept :
      nf7::File(kType, env),
      nf7::DirItem(nf7::DirItem::kTooltip),
      q_(std::make_shared<Queue>(*this)) {
  }

  FontContext(nf7::Deserializer& ar) : FontContext(ar.env()) {
  }
  void Serialize(nf7::Serializer&) const noexcept override {
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<FontContext>(env);
  }

  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::font::Queue>(t).Select(this, q_.get());
  }

 private:
  std::shared_ptr<Queue> q_;
};

class FontContext::Queue final : public nf7::font::Queue,
    public std::enable_shared_from_this<FontContext::Queue> {
 public:
  struct SharedData final {
   public:
    std::atomic<bool> broken = false;
    FT_Library        ft;
  };
  struct Runner final {
   public:
    Runner(const std::shared_ptr<SharedData>& d) noexcept : data_(d) {
    }
    void operator()(Task&& t) noexcept {
      if (!data_->broken) {
        t(data_->ft);
      }
    }
   private:
    std::shared_ptr<SharedData> data_;
  };
  using Thread = nf7::Thread<Runner, Task>;

  Queue(FontContext& f) noexcept :
      data_(std::make_shared<SharedData>()),
      th_(std::make_shared<Thread>(f, Runner {data_})) {
    th_->Push(th_, [data = data_](auto) {
      if (const auto err = FT_Init_FreeType(&data->ft)) {
        data->broken = true;
      }
    });
  }
  ~Queue() noexcept {
    th_->Push(th_, [](auto ft) {
      FT_Done_FreeType(ft);
    });
  }

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& task) noexcept override {
    th_->Push(ctx, std::move(task));
  }
  std::shared_ptr<nf7::font::Queue> self() noexcept override {
    return shared_from_this();
  }

  bool broken() const noexcept { return data_->broken; }
  size_t tasksDone() const noexcept { return th_->tasksDone(); }

 private:
  std::shared_ptr<SharedData> data_;
  std::shared_ptr<Thread>     th_;
};


void FontContext::UpdateTooltip() noexcept {
  ImGui::Text("status    : %s", q_->broken()? "broken": "running");
  ImGui::Text("tasks done: %zu", q_->tasksDone());
}

}  // namespace
}  // namespace nf7
