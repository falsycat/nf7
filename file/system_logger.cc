#include <cinttypes>
#include <deque>
#include <memory>
#include <typeinfo>
#include <utility>
#include <iostream>

#include "nf7.hh"

#include "common/dir.hh"
#include "common/gui_window.hh"
#include "common/logger.hh"
#include "common/logger_pool.hh"
#include "common/ptr_selector.hh"
#include "common/type_info.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Logger final : public File, public nf7::DirItem, public nf7::Logger {
 public:
  static inline const GenericTypeInfo<Logger> kType = {"System/Logger", {"DirItem"}};

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

  Logger(Env& env, uint32_t max_rows = 1024, bool propagate = false, bool freeze = false) noexcept :
      File(kType, env), DirItem(DirItem::kMenu),
      propagation_pool_(*this, 1), win_(*this, "LogView System/Logger"),
      max_rows_(max_rows), propagate_(propagate), freeze_(freeze) {
    win_.shown() = true;
  }

  Logger(Env& env, Deserializer& ar) : Logger(env) {
    ar(win_, max_rows_, propagate_, freeze_);

    if (max_rows_ == 0) {
      throw DeserializeException("max_rows must be 1 or more");
    }
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(win_, max_rows_, propagate_, freeze_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Logger>(env, max_rows_, propagate_, freeze_);
  }

  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateRowMenu(const Row&) noexcept;

  void Write(Item&& item) noexcept override {
    if (freeze_) return;
    if (rows_.size() >= max_rows_) rows_.pop_front();

    if (propagate_) {
      propagation_pool_.Write(Item(item));
    }

    Row row = {
      .file     = item.file,
      .srcloc   = item.srcloc,
      .level    = GetLevelString(item.level),
      .msg      = std::move(item.msg),
      .path     = GetPathString(item.file),
      .location = GetLocationString(item.srcloc),
    };
    rows_.push_back(std::move(row));

    updated_ = true;
  }

  File::Interface* iface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::DirItem, nf7::Logger>(t).Select(this);
  }

 private:
  std::deque<Row> rows_;
  const char* popup_ = nullptr;

  LoggerPool propagation_pool_;

  bool updated_ = false;

  // persistent params
  gui::Window win_;
  uint32_t max_rows_;
  bool propagate_;
  bool freeze_;


  void DropExceededRows() noexcept {
    if (rows_.size() <= max_rows_) return;
    rows_.erase(rows_.begin(), rows_.end()-max_rows_);
  }


  std::string GetPathString(File::Id id) const noexcept
  try {
    return env().GetFile(id).abspath().Stringify();
  } catch (File::NotFoundException&) {
    return "[EXPIRED]";
  }
  static const char* GetLevelString(Level lv) noexcept {
    switch (lv) {
    case kTrace:
      return "TRAC";
    case kInfo:
      return "INFO";
    case kWarn:
      return "WARN";
    case kError:
      return "ERRR";
    default:
      assert(false);
    }
  }
  static std::string GetLocationString(const std::source_location loc) noexcept {
    return loc.file_name()+":"s+loc.function_name()+":"s+std::to_string(loc.line());
  }
};

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
    if (ImGui::DragScalar("max rows", ImGuiDataType_U32, &max_rows_, 1, &kMinRows, &kMaxRows)) {
      DropExceededRows();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("the oldest row is dropped when exceed");
    }

    ImGui::Checkbox("propagate", &propagate_);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("after handling, passes the msg to outer logger if exists");
    }

    ImGui::Checkbox("freeze", &freeze_);
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
      const bool autoscroll =
          std::exchange(updated_, false) && ImGui::GetScrollY() == ImGui::GetScrollMaxY();

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
