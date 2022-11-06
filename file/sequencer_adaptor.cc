#include "nf7.hh"

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>

#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/gui_value.hh"
#include "common/life.hh"
#include "common/ptr_selector.hh"
#include "common/sequencer.hh"
#include "common/value.hh"


namespace nf7 {
namespace {

class Adaptor final : public nf7::FileBase, public nf7::Sequencer {
 public:
  static inline const nf7::GenericTypeInfo<Adaptor> kType =
      {"Sequencer/Adaptor", {"nf7::Sequencer"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Wraps and Adapts other Sequencer.");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "implements nf7::Sequencer");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "changes will be applied to active lambdas immediately");
  }

  class Session;
  class Lambda;
  class Editor;

  struct Var {
    std::string name;
    bool peek = false;

    void serialize(auto& ar) {
      ar(name, peek);
    }
  };
  struct Data {
    nf7::File::Path path;
    std::vector<std::pair<std::string, nf7::gui::Value>> input_imm;
    std::vector<std::pair<std::string, Var>>             input_map;
    std::vector<std::pair<std::string, std::string>>     output_map;

    void serialize(auto& ar) {
      ar(path, input_imm, input_map, output_map);
    }
  };

  Adaptor(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      Sequencer(Sequencer::kCustomItem |
                Sequencer::kTooltip |
                Sequencer::kParamPanel),
      life_(*this), mem_(std::move(d), *this) {
  }

  Adaptor(nf7::Deserializer& ar) : Adaptor(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Adaptor>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Sequencer::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Context>&) noexcept override;

  void UpdateItem(nf7::Sequencer::Editor&) noexcept override;
  void UpdateParamPanel(nf7::Sequencer::Editor&) noexcept override;
  void UpdateTooltip(nf7::Sequencer::Editor&) noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Memento, nf7::Sequencer>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Adaptor> life_;

  nf7::GenericMemento<Data> mem_;
};


class Adaptor::Session final : public nf7::Sequencer::Session {
 public:
  // ensure that Adaptor is alive
  Session(Adaptor& f, const std::shared_ptr<nf7::Sequencer::Session>& parent) noexcept : parent_(parent) {
    for (auto& p : f.mem_->input_imm) {
      vars_[p.first] = p.second.entity();
    }
    for (auto& p : f.mem_->input_map) {
      if (p.second.name.size() == 0) continue;
      if (p.second.peek) {
        if (const auto ptr = parent->Peek(p.second.name)) {
          vars_[p.first] = *ptr;
        }
      } else {
        if (auto ptr = parent->Receive(p.second.name)) {
          vars_[p.first] = std::move(*ptr);
        }
      }
    }
    for (auto& p : f.mem_->output_map) {
      outs_[p.first] = p.second;
    }
  }

  const nf7::Value* Peek(std::string_view name) noexcept override {
    auto itr = vars_.find(std::string {name});
    return itr != vars_.end()? &itr->second: nullptr;
  }
  std::optional<nf7::Value> Receive(std::string_view name) noexcept override {
    auto itr = vars_.find(std::string {name});
    if (itr == vars_.end()) {
      return std::nullopt;
    }
    auto ret = std::move(itr->second);
    vars_.erase(itr);
    return ret;
  }

  void Send(std::string_view name, nf7::Value&& v) noexcept override {
    if (done_) return;

    auto itr = outs_.find(std::string {name});
    if (itr != outs_.end()) {
      parent_->Send(itr->second, std::move(v));
    }
  }

  void Finish() noexcept override {
    assert(parent_);
    parent_->Finish();
    done_ = true;
  }

 private:
  std::shared_ptr<nf7::Sequencer::Session> parent_;

  std::unordered_map<std::string, nf7::Value> vars_;
  std::unordered_map<std::string, std::string> outs_;

  bool done_ = false;
};
class Adaptor::Lambda final : public nf7::Sequencer::Lambda,
    public std::enable_shared_from_this<Adaptor::Lambda> {
 public:
  Lambda(Adaptor& f, const std::shared_ptr<nf7::Context>& parent) noexcept :
      nf7::Sequencer::Lambda(f, parent), f_(f.life_) {
  }

  void Run(const std::shared_ptr<nf7::Sequencer::Session>& ss) noexcept override
  try {
    f_.EnforceAlive();

    auto& target = f_->ResolveOrThrow(f_->mem_->path);
    auto& seq    = target.interfaceOrThrow<nf7::Sequencer>();
    if (!la_ || target.id() != cached_id_) {
      la_        = seq.CreateLambda(shared_from_this());
      cached_id_ = target.id();
    }
    la_->Run(std::make_shared<Adaptor::Session>(*f_, ss));
  } catch (nf7::Exception&) {
    ss->Finish();
  }

 private:
  nf7::Life<Adaptor>::Ref f_;

  nf7::File::Id cached_id_ = 0;
  std::shared_ptr<nf7::Sequencer::Lambda> la_;
};
std::shared_ptr<nf7::Sequencer::Lambda> Adaptor::CreateLambda(
    const std::shared_ptr<nf7::Context>& parent) noexcept {
  return std::make_shared<Adaptor::Lambda>(*this, parent);
}


class Adaptor::Editor final : public nf7::Sequencer::Editor {
 public:
  using nf7::Sequencer::Editor::Editor;
};


void Adaptor::UpdateItem(Sequencer::Editor&) noexcept {
  try {
    auto& seq = ResolveOrThrow(mem_->path).interfaceOrThrow<nf7::Sequencer>();
    if (seq.flags() & nf7::Sequencer::kCustomItem) {
      Adaptor::Editor ed;
      seq.UpdateItem(ed);
    }
  } catch (nf7::File::NotFoundException&) {
    ImGui::TextUnformatted("file missing");
  } catch (nf7::File::NotImplementedException&) {
    ImGui::TextUnformatted("file does not have Sequencer interface");
  }
}
void Adaptor::UpdateParamPanel(Sequencer::Editor&) noexcept {
  bool commit = false;

  auto& imm     = mem_->input_imm;
  auto& inputs  = mem_->input_map;
  auto& outputs = mem_->output_map;

  const auto em = ImGui::GetFontSize();
  if (ImGui::CollapsingHeader("Sequencer/Adaptor", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (nf7::gui::PathButton("path", mem_->path, *this)) {
      commit = true;
    }

    if (ImGui::BeginTable("table", 3)) {
      ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 1.f);
      ImGui::TableSetupColumn("arrow", ImGuiTableColumnFlags_WidthFixed, 1*em);
      ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.f);

      // ---- immediate values
      ImGui::PushID("imm");
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Spacing();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("imm input");
      ImGui::SameLine();
      if (ImGui::Button("+")) {
        imm.push_back({"target_input", {}});
        commit = true;
      }
      if (imm.size() == 0) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("no rule");
      }
      for (size_t i = 0; i < imm.size(); ++i) {
        auto& p = imm[i];

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));

        if (ImGui::TableNextColumn()) {
          commit |= p.second.UpdateTypeButton("T");
          ImGui::SameLine();
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
          commit |= p.second.UpdateEditor();
        }
        if (ImGui::TableNextColumn()) {
          ImGui::TextUnformatted("->");
        }
        if (ImGui::TableNextColumn()) {
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
          ImGui::InputTextWithHint("##name", "dst", &p.first);
          commit |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopID();
      }
      ImGui::PopID();

      // ---- input map
      ImGui::PushID("input");
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Spacing();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("input");
      ImGui::SameLine();
      if (ImGui::Button("+")) {
        inputs.push_back({"target_input", {}});
        commit = true;
      }
      if (inputs.size() == 0) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("no rule");
      }
      for (size_t i = 0; i < inputs.size(); ++i) {
        auto& in = inputs[i];

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));

        if (ImGui::TableNextColumn()) {
          const char* text = in.second.peek? "P": "R";
          if (ImGui::Button(text)) {
            in.second.peek = !in.second.peek;
            commit = true;
          }
          ImGui::SameLine();
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
          ImGui::InputTextWithHint("##src", "src", &in.second.name);
          commit |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (ImGui::TableNextColumn()) {
          ImGui::TextUnformatted("->");
        }
        if (ImGui::TableNextColumn()) {
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
          ImGui::InputTextWithHint("##dst", "dst", &in.first);
          commit |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopID();
      }
      ImGui::PopID();

      // ---- output map
      ImGui::PushID("output");
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Spacing();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("output");
      ImGui::SameLine();
      if (ImGui::Button("+")) {
        outputs.push_back({"target_output", ""});
        commit = true;
      }
      if (outputs.size() == 0) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("no rule");
      }
      for (size_t i = 0; i < outputs.size(); ++i) {
        auto& out = outputs[i];

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));

        if (ImGui::TableNextColumn()) {
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
          ImGui::InputTextWithHint("##src", "src", &out.first);
          commit |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (ImGui::TableNextColumn()) {
          ImGui::TextUnformatted("->");
        }
        if (ImGui::TableNextColumn()) {
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
          ImGui::InputTextWithHint("##dst", "dst", &out.second);
          commit |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopID();
      }
      ImGui::PopID();
      ImGui::EndTable();
    }
  }
  if (commit) {
    imm.erase(
        std::remove_if(
            imm.begin(), imm.end(),
            [](auto& x) { return x.first.size() == 0; }),
        imm.end());
    inputs.erase(
        std::remove_if(
            inputs.begin(), inputs.end(),
            [](auto& x) { return x.first.size() == 0; }),
        inputs.end());
    outputs.erase(
        std::remove_if(
            outputs.begin(), outputs.end(),
            [](auto& x) { return x.first.size() == 0; }),
        outputs.end());
    mem_.Commit();
  }

  ImGui::Spacing();
  try {
    auto& seq = ResolveOrThrow(mem_->path).interfaceOrThrow<nf7::Sequencer>();
    if (seq.flags() & nf7::Sequencer::kParamPanel) {
      Adaptor::Editor ed;
      seq.UpdateParamPanel(ed);
    }
  } catch (nf7::Exception&) {
    ImGui::Separator();
    ImGui::TextUnformatted("TARGET HAS NO SEQUENCER INTERFACE");
  }
}
void Adaptor::UpdateTooltip(Sequencer::Editor&) noexcept {
  ImGui::TextUnformatted("Sequencer/Adaptor");
}

}
}  // namespace nf7
