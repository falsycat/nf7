#include <atomic>
#include <cinttypes>
#include <deque>
#include <memory>
#include <mutex>
#include <typeinfo>
#include <utility>
#include <iostream>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_type_info.hh"
#include "common/gui_window.hh"
#include "common/logger.hh"
#include "common/ptr_selector.hh"
#include "common/yas_std_atomic.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Logger final : public nf7::File,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<Logger> kType = {
    "System/Logger", {"nf7::DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Records log output from other files.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Logger");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "logged are children and grandchildren of a dir that has this with name '_logger'");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "recorded logs won't be permanentized");
  }

  struct Row final {
   public:
    File::Id file;
    std::source_location srcloc;

    const char* level;
    std::string msg;
    std::string path;
    std::string location;

    std::string Stringify() const noexcept {
      std::stringstream st;
      st << level << "\n";
      st << "  " << msg << "\n";
      st << "    from " << path << "\n";
      st << "    at   " << location;
      return st.str();
    }
  };
  struct Param final {
   public:
    Param(uint32_t mr, bool p, bool f) : max_rows(mr), propagate(p), freeze(f) {
    }
    std::atomic<uint32_t> max_rows;
    std::atomic<bool>     propagate;
    std::atomic<bool>     freeze;
  };
  class ItemStore;

  Logger(Env& env, uint32_t max_rows = 1024, bool propagate = false, bool freeze = false) noexcept :
      File(kType, env), DirItem(DirItem::kMenu),
      param_(std::make_shared<Param>(max_rows, propagate, freeze)),
      win_(*this, "LogView") {
    win_.shown() = true;
  }

  Logger(Env& env, Deserializer& ar) : Logger(env) {
    ar(win_, param_->max_rows, param_->propagate, param_->freeze);

    if (param_->max_rows == 0) {
      throw DeserializeException("max_rows must be 1 or more");
    }
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(win_, param_->max_rows, param_->propagate, param_->freeze);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Logger>(
        env, param_->max_rows, param_->propagate, param_->freeze);
  }

  void Handle(const Event& ev) noexcept override;
  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateRowMenu(const Row&) noexcept;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::DirItem, nf7::Logger>(t).
        Select(this, store_.get());
  }

 private:
  std::shared_ptr<Param>     param_;
  std::shared_ptr<ItemStore> store_;
  std::deque<Row>            rows_;

  const char* popup_ = nullptr;

  gui::Window win_;


  void DropExceededRows() noexcept {
    if (rows_.size() <= param_->max_rows) return;
    rows_.erase(rows_.begin(), rows_.end()-param_->max_rows);
  }

  std::string GetPathString(File::Id id) const noexcept
  try {
    return env().GetFileOrThrow(id).abspath().Stringify();
  } catch (ExpiredException&) {
    return "[EXPIRED]";
  }
  static const char* GetLevelString(nf7::Logger::Level lv) noexcept {
    switch (lv) {
    case nf7::Logger::kTrace:
      return "TRAC";
    case nf7::Logger::kInfo:
      return "INFO";
    case nf7::Logger::kWarn:
      return "WARN";
    case nf7::Logger::kError:
      return "ERRR";
    default:
      assert(false);
      return "X(";
    }
  }
  static std::string GetLocationString(const std::source_location loc) noexcept {
    return loc.file_name()+":"s+loc.function_name()+":"s+std::to_string(loc.line());
  }
};

class Logger::ItemStore final : public nf7::Context,
    public nf7::Logger,
    public std::enable_shared_from_this<ItemStore> {
 public:
  ItemStore() = delete;
  ItemStore(File& owner, const std::shared_ptr<Param>& param) noexcept :
      Context(owner.env(), owner.id()), param_(param) {
  }
  ItemStore(const ItemStore&) = delete;
  ItemStore(ItemStore&&) = delete;
  ItemStore& operator=(const ItemStore&) = delete;
  ItemStore& operator=(ItemStore&&) = delete;

  void Write(nf7::Logger::Item&& item) noexcept override {
    if (param_->freeze) return;
    if (param_->propagate) {
      // TODO propagation
    }

    std::unique_lock<std::mutex> k(mtx_);
    if (items_.size() >= param_->max_rows) items_.pop_front();
    items_.push_back(std::move(item));
  }
  bool MoveItemsTo(auto& owner) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (items_.empty()) return false;
    auto& rows = owner.rows_;

    auto itr = items_.begin();
    if (rows.size()+items_.size() > param_->max_rows) {
      // max_rows may be changed
      if (items_.size() > param_->max_rows) {
        itr += static_cast<intmax_t>(param_->max_rows - items_.size());
      }
      const auto keep =
          static_cast<intmax_t>(param_->max_rows) - std::distance(itr, items_.end());
      rows.erase(rows.begin(), rows.end()-keep);
    }
    for (; itr < items_.end(); ++itr) {
      Row row = {
        .file     = itr->file,
        .srcloc   = itr->srcloc,
        .level    = GetLevelString(itr->level),
        .msg      = std::move(itr->msg),
        .path     = owner.GetPathString(itr->file),
        .location = GetLocationString(itr->srcloc),
      };
      rows.push_back(std::move(row));
    }
    items_.clear();
    return true;
  }

  std::string GetDescription() const noexcept override {
    return "System/Logger shared instance";
  }

  std::shared_ptr<nf7::Logger> self(nf7::Logger*) noexcept override {
    return shared_from_this();
  }

 private:
  std::mutex mtx_;
  std::deque<nf7::Logger::Item> items_;
  std::shared_ptr<Param> param_;
};

void Logger::Handle(const Event& ev) noexcept {
  switch (ev.type) {
  case Event::kAdd:
    store_ = std::make_shared<ItemStore>(*this, param_);
    return;
  case Event::kRemove:
    store_ = nullptr;
    return;
  default:
    return;
  }
}

void Logger::Update() noexcept {
  if (const auto name = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(name);
  }

  const auto em = ImGui::GetFontSize();

  // config popup
  if (ImGui::BeginPopup("ConfigPopup")) {
    ImGui::TextUnformatted("System/Logger Config");
    ImGui::Spacing();

    static const uint32_t kMinRows = 1, kMaxRows = 1024*1024;
    uint32_t max_rows = param_->max_rows;
    if (ImGui::DragScalar("max rows", ImGuiDataType_U32, &max_rows, 1, &kMinRows, &kMaxRows)) {
      param_->max_rows = max_rows;
      DropExceededRows();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("the oldest row is dropped when exceed");
    }

    bool propagate = param_->propagate;
    if (ImGui::Checkbox("propagate", &propagate)) {
      param_->propagate = propagate;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("after handling, passes the msg to outer logger if exists");
    }

    bool freeze = param_->freeze;
    if (ImGui::Checkbox("freeze", &freeze)) {
      param_->freeze = freeze;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("stop handling except propagation");
    }
    ImGui::EndPopup();
  }

  // LogView
  const auto kInit = [em]() {
    ImGui::SetNextWindowSize({48*em, 16*em}, ImGuiCond_FirstUseEver);
  };
  if (win_.Begin(kInit)) {
    constexpr auto kTableFlags =
        ImGuiTableFlags_Resizable         |
        ImGuiTableFlags_Hideable          |
        ImGuiTableFlags_RowBg             |
        ImGuiTableFlags_Borders           |
        ImGuiTableFlags_ContextMenuInBody |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("logs", 4, kTableFlags, ImGui::GetContentRegionAvail(), 0)) {
      const bool updated    = store_->MoveItemsTo(*this);
      const bool autoscroll = updated && ImGui::GetScrollY() == ImGui::GetScrollMaxY();

      ImGui::TableSetupColumn("level");
      ImGui::TableSetupColumn("msg");
      ImGui::TableSetupColumn("path");
      ImGui::TableSetupColumn("location");
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableHeadersRow();

      for (const auto& row : rows_) {
        ImGui::TableNextRow();
        ImGui::PushID(&row);

        if (autoscroll && &row == &rows_.back()) {
          ImGui::SetScrollHereY();
        }

        // level column
        if (ImGui::TableSetColumnIndex(0)) {
          constexpr auto kFlags =
              ImGuiSelectableFlags_SpanAllColumns |
              ImGuiSelectableFlags_AllowItemOverlap;
          ImGui::Selectable(row.level, false, kFlags);
          if (ImGui::BeginPopupContextItem()) {
            UpdateRowMenu(row);
            ImGui::EndPopup();
          }
        }
        // msg column
        if (ImGui::TableNextColumn()) {
          ImGui::TextUnformatted(row.msg.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(row.msg.c_str());
          }
        }
        // path column
        if (ImGui::TableNextColumn()) {
          ImGui::TextUnformatted(row.path.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(row.path.c_str());
          }
        }
        // location column
        if (ImGui::TableNextColumn()) {
          ImGui::TextUnformatted(row.location.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("file: %s", row.srcloc.file_name());
            ImGui::Text("func: %s", row.srcloc.function_name());
            ImGui::Text("line: %zu", static_cast<size_t>(row.srcloc.line()));
            ImGui::Text("col : %zu", static_cast<size_t>(row.srcloc.column()));
            ImGui::EndTooltip();
          }
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
  }
  win_.End();
}
void Logger::UpdateMenu() noexcept {
  ImGui::MenuItem("shown",  nullptr, &win_.shown());

  if (ImGui::MenuItem("config")) {
    popup_ = "ConfigPopup";
  }
}
void Logger::UpdateRowMenu(const Row& row) noexcept {
  if (ImGui::MenuItem("copy as text")) {
    ImGui::SetClipboardText(row.Stringify().c_str());
  }
}

}
}  // namespace nf7
