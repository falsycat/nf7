#include <atomic>
#include <cinttypes>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <iostream>

#include <imgui.h>

#include <yaml-cpp/yaml.h>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_config.hh"
#include "common/gui_window.hh"
#include "common/life.hh"
#include "common/logger.hh"
#include "common/logger_ref.hh"
#include "common/ptr_selector.hh"
#include "common/yas_std_atomic.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class Logger final : public nf7::File,
    public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<Logger> kType = {
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
    std::exception_ptr ex;

    std::string Stringify() const noexcept {
      std::stringstream st;
      st << level << "\n";
      st << "  " << msg << "\n";
      st << "    from " << path << "\n";
      st << "    at   " << location;
      return st.str();
    }
  };

  struct Data {
    uint32_t max_rows  = 1024;
    bool     propagate = false;
    bool     freeze    = false;

    Data() noexcept { }
    void serialize(auto& ar) {
      ar(max_rows, propagate, freeze);
      if (max_rows == 0) {
        throw DeserializeException("max_rows must be 1 or more");
      }
    }

    std::string Stringify() const noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "max_rows";
      st << YAML::Value << max_rows;
      st << YAML::Key   << "propagate";
      st << YAML::Value << propagate;
      st << YAML::Key   << "freeze";
      st << YAML::Value << freeze;
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str)
    try {
      const auto yaml = YAML::Load(str);

      Data d;
      d.max_rows  = yaml["max_rows"].as<uint32_t>();
      d.propagate = yaml["propagate"].as<bool>();
      d.freeze    = yaml["freeze"].as<bool>();

      *this = std::move(d);
    } catch (YAML::Exception& e) {
      throw nf7::Exception {e.what()};
    }
  };

  Logger(nf7::Env& env, Data&& d = {}) noexcept :
      File(kType, env), DirItem(DirItem::kMenu),
      mem_(std::move(d), *this), win_(*this, "Log View") {
    win_.shown() = true;

    mem_.onCommit = mem_.onRestore = [this]() {
      store_->param(mem_.data());
    };
  }

  Logger(nf7::Deserializer& ar) : Logger(ar.env()) {
    ar(win_, mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(win_, mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Logger>(env, Data {mem_.data()});
  }

  void Handle(const nf7::File::Event& ev) noexcept override {
    switch (ev.type) {
    case Event::kAdd:
      store_ = std::make_shared<ItemStore>(*this);
      return;
    case Event::kRemove:
      store_ = nullptr;
      return;
    default:
      return;
    }
  }

  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateRowMenu(const Row&) noexcept;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::DirItem, nf7::Logger>(t).
        Select(this, store_.get());
  }

 private:
  class ItemStore;
  std::shared_ptr<ItemStore> store_;
  std::deque<Row>            rows_;

  nf7::GenericMemento<Data> mem_;

  nf7::gui::Window win_;


  void DropExceededRows() noexcept {
    if (rows_.size() <= mem_->max_rows) return;
    rows_.erase(rows_.begin(), rows_.end()-mem_->max_rows);
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
    return loc.file_name()+":"s+std::to_string(loc.line());
  }


  class ItemStore final : public nf7::Context,
      public nf7::Logger,
      public std::enable_shared_from_this<ItemStore> {
   public:
    ItemStore() = delete;
    ItemStore(File& f) noexcept : nf7::Context(f) {
    }
    ItemStore(const ItemStore&) = delete;
    ItemStore(ItemStore&&) = delete;
    ItemStore& operator=(const ItemStore&) = delete;
    ItemStore& operator=(ItemStore&&) = delete;

    void Write(nf7::Logger::Item&& item) noexcept override {
      if (param_.freeze) return;
      if (param_.propagate) {
        // TODO propagation
      }

      std::unique_lock<std::mutex> k(mtx_);
      if (items_.size() >= param_.max_rows) items_.pop_front();
      items_.push_back(std::move(item));
    }

    bool MoveItemsTo(auto& owner) noexcept {
      std::unique_lock<std::mutex> k(mtx_);
      if (items_.empty()) return false;
      auto& rows = owner.rows_;

      auto itr = items_.begin();
      if (rows.size()+items_.size() > param_.max_rows) {
        if (items_.size() > param_.max_rows) {
          itr += static_cast<intmax_t>(param_.max_rows - items_.size());
        }
        const auto keep =
            static_cast<intmax_t>(param_.max_rows) - std::distance(itr, items_.end());
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
          .ex       = itr->ex,
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

    void param(const Data& d) noexcept {
      std::unique_lock<std::mutex> k(mtx_);
      param_ = d;
    }

   private:
    std::mutex mtx_;
    std::deque<nf7::Logger::Item> items_;

    Data param_;
  };
};


void Logger::Update() noexcept {
  const auto em = ImGui::GetFontSize();

  if (win_.shownInCurrentFrame()) {
    ImGui::SetNextWindowSize({48*em, 16*em}, ImGuiCond_FirstUseEver);
  }
  if (win_.Begin()) {
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
            ImGui::Text(row.location.c_str());
            for (auto ptr = row.ex; ptr;)
            try {
              ImGui::Bullet();
              std::rethrow_exception(ptr);
            } catch (Exception& e) {
              e.UpdatePanic();
              ImGui::Spacing();
              ptr = e.reason();
            } catch (std::exception& e) {
              ImGui::Text("std::exception (%s)", e.what());
              ptr = nullptr;
            }
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
  if (ImGui::BeginMenu("config")) {
    nf7::gui::Config(mem_);
    ImGui::EndMenu();
  }
  ImGui::Separator();
  ImGui::MenuItem("Log View",  nullptr, &win_.shown());
}
void Logger::UpdateRowMenu(const Row& row) noexcept {
  if (ImGui::MenuItem("copy as text")) {
    ImGui::SetClipboardText(row.Stringify().c_str());
  }
  ImGui::Separator();
  if (ImGui::MenuItem("clear")) {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this), [this]() { rows_.clear(); });
  }
}

}
}  // namespace nf7
