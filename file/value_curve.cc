#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include <ImNodes.h>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/sequencer.hh"
#include "common/value.hh"
#include "common/yas_imgui.hh"


namespace nf7 {
namespace {

class Curve final : public nf7::FileBase,
    public nf7::DirItem,
    public nf7::Node,
    public nf7::Sequencer {
 public:
  static inline const nf7::GenericTypeInfo<Curve> kType = {
    "Value/Curve", {"nf7::DirItem", "nf7::Node", "nf7::Sequencer"},
    "bezier curve editor",
  };

  class NodeLambda;
  class SeqLambda;

  struct Term {
    ImVec2 p1, p2, p3;  // p4 is next point's p1
    uint64_t id;  // id is not saved

    bool break_prev = false;

    void serialize(auto& ar) {
      ar(p1, p2, p3, break_prev);
    }
  };
  struct Data {
    std::vector<Term> terms = {
      {.p1 = {0, 0}, .p2 = {0, 0}, .p3 = {1, 1}, .id = 0,},
      {.p1 = {1, 1}, .p2 = {1, 1}, .p3 = {1, 1}, .id = 0,}};

    Data() {}
    void serialize(auto& ar) {
      ar(terms);
    }
  };

  Curve(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kWidget),
      nf7::Node(nf7::Node::kCustomNode),
      nf7::Sequencer(nf7::Sequencer::kCustomItem |
                     nf7::Sequencer::kParamPanel),
      life_(*this), mem_(*this, std::move(data)) {
    AssignId();
    Sanitize();
  }

  Curve(nf7::Deserializer& ar) : Curve(ar.env()) {
    ar(mem_.data());
    AssignId();
    Sanitize();
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Curve>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  std::shared_ptr<nf7::Sequencer::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Context>&) noexcept override;

  nf7::Node::Meta GetMeta() const noexcept override {
    return {{"x"}, {"y"}};
  }

  void UpdateItem(nf7::Sequencer::Editor&) noexcept override;
  void UpdateParamPanel(nf7::Sequencer::Editor&) noexcept override;
  void UpdateNode(nf7::Node::Editor&) noexcept override;
  void UpdateWidget() noexcept override;
  void UpdateCurveEditorWindow(const ImVec2&) noexcept;
  void UpdateCurveEditor(const ImVec2&) noexcept;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node, nf7::Sequencer>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Curve> life_;

  uint64_t next_id_ = 1;

  nf7::GenericMemento<Data> mem_;

  // GUI parameters
  std::unordered_set<uint64_t> selected_;
  bool last_action_moved_ = false;


  void AddPoint(const ImVec2& pos) noexcept {
    auto& terms = mem_.data().terms;

    const auto x = std::clamp(pos.x, 0.f, 1.f);
    auto itr = std::find_if(terms.begin(), terms.end(),
                            [x](auto& a) { return x <= a.p1.x; });
    assert(itr != terms.end());
    if (itr == terms.begin()) {
      ++itr;
    }
    auto& pt = *(itr-1);
    auto  nt = itr+1 < terms.end()? &*(itr+1): nullptr;

    auto p3 = pt.p3;
    pt.p3 = pos;

    pt.p2.x = std::clamp(pt.p2.x, pt.p1.x, pos.x);
    p3.x    = std::clamp(p3.x, pos.x, nt? nt->p1.x: 1.f);

    terms.insert(itr, {.p1 = pos, .p2 = pos, .p3 = p3, .id = next_id_++});
  }
  void RemoveSelectedPoints() noexcept {
    auto& terms = mem_.data().terms;
    assert(terms.size() >= 2);

    terms.erase(
        std::remove_if(terms.begin()+1, terms.end()-1,
                       [&](auto& x) { return selected_.contains(x.id); }),
        terms.end()-1);
    selected_.clear();
  }
  void ResetControlsOfSelectedPoints() noexcept {
    auto& terms = mem_.data().terms;
    for (auto id : selected_) {
      auto itr = std::find_if(terms.begin(), terms.end(),
                              [id](auto& x) { return x.id == id; });
      if (itr == terms.end()) {
        continue;
      }

      auto& t  = *itr;
      auto  pt = itr > terms.begin()? &*(itr-1): nullptr;
      if (pt) {
        pt->p3 = t.p1;
      }
      t.p2 = t.p1;
    }
  }
  void MovePoint(ImVec2 diff) noexcept {
    auto& terms = mem_.data().terms;
    for (auto id : selected_) {
      auto itr = std::find_if(terms.begin(), terms.end(),
                              [id](auto& x) { return x.id == id; });
      if (itr == terms.end()) {
        continue;
      }

      auto& t  = *itr;
      auto  pt = itr > terms.begin()? &*(itr-1): nullptr;
      auto  nt = itr+1 < terms.end()? &*(itr+1): nullptr;

      const auto pp1 = t.p1;
      t.p1 += diff;
      t.p1.x = std::clamp(t.p1.x, 0.f, 1.f);
      t.p1.y = std::clamp(t.p1.y, 0.f, 1.f);

      if (!pt) {
        t.p1.x = 0;
      } else if (!nt) {
        t.p1.x = 1;
      }

      const auto adiff = t.p1 - pp1;
      t.p2   += adiff;
      t.p2.x  = std::clamp(t.p2.x, t.p1.x, nt? nt->p1.x: 0);
      t.p2.y  = std::clamp(t.p2.y, 0.f, 1.f);
      t.p3.x  = std::clamp(t.p3.x, t.p1.x, nt? nt->p1.x: 0);

      if (pt) {
        pt->p3   += adiff;
        pt->p3.x  = std::clamp(pt->p3.x, pt->p1.x, t.p1.x);
        pt->p3.y  = std::clamp(pt->p3.y, 0.f, 1.f);
        pt->p2.x  = std::clamp(pt->p2.x, pt->p1.x, t.p1.x);
      }
    }
  }

  void SelectPoint(uint64_t id, bool single = !ImGui::GetIO().KeyCtrl) noexcept {
    if (single) {
      selected_.clear();
    }
    selected_.insert(id);
  }

  void AssignId() noexcept {
    for (auto& term : mem_.data().terms) {
      term.id = next_id_++;
    }
  }
  void Sanitize() noexcept {
    auto& terms = mem_.data().terms;
    std::sort(terms.begin(), terms.end(),
              [](auto& a, auto& b) {
                return
                    a.p1.x < b.p1.x? true: a.p1.x == b.p1.x? a.id < b.id: false;
              });

    for (auto itr = terms.begin(); itr+1 < terms.end(); ++itr) {
      auto& a = *itr;
      auto& b = *(itr+1);

      a.p2.x = std::clamp(a.p2.x, a.p1.x, b.p1.x);
      a.p3.x = std::clamp(a.p3.x, a.p1.x, b.p1.x);
    }
  }

  double Calc(double x) const noexcept {
    const auto& terms = mem_.data().terms;
    assert(terms.size() >= 2);

    x = std::clamp(x, 0., 1.);

    auto r_itr = std::find_if(terms.begin(), terms.end(),
                              [x](auto& a) { return x <= a.p1.x; });
    assert(r_itr != terms.end());
    if (r_itr == terms.begin()) {
      return static_cast<double>(r_itr->p1.y);
    }
    auto l_itr = r_itr-1;

    const auto lx   = static_cast<double>(l_itr->p1.x);
    const auto rx   = static_cast<double>(r_itr->p1.x);
    const auto xlen = rx-lx;
    if (xlen == 0) {
      return l_itr->p1.y;
    }

    const auto ly   = static_cast<double>(l_itr->p1.y);
    const auto ry   = static_cast<double>(r_itr->p1.y);
    const auto ylen = ry-ly;

    const auto xf = (x-lx)/xlen;
    const auto x1 = (static_cast<double>(l_itr->p2.x)-lx)/xlen;
    const auto y1 = (static_cast<double>(l_itr->p2.y)-ly)/ylen;
    const auto x2 = (static_cast<double>(l_itr->p3.x)-lx)/xlen;
    const auto y2 = (static_cast<double>(l_itr->p3.y)-ly)/ylen;

    const auto b = Bezier(xf, x1, y1, x2, y2);
    return b*ylen + ly;
  }
  static double Bezier(double x, double x1, double y1, double x2, double y2) noexcept {
    double a = 0.5;
    double t = 0.5;
    for (;;) {
      const auto rt = 1-t;

      const auto xt   = 3*t*rt*rt*x1 + 3*t*t*rt*x2 + t*t*t;
      const auto diff = std::abs(xt - x);
      if (diff < 1e-2) {
        break;
      }

      a /= 2;
      if (xt > x) {
        t -= a;
      } else if (xt < x) {
        t += a;
      }
    }
    const auto rt = 1-t;
    return 3*t*rt*rt*y1 + 3*t*t*rt*y2 + t*t*t;
  }
};


class Curve::NodeLambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Curve::NodeLambda> {
 public:
  NodeLambda(Curve& f, const std::shared_ptr<Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();
    in.sender->Handle("y", f_->Calc(in.value.scalar()), shared_from_this());
  } catch (nf7::Exception&) {
  }

 private:
  nf7::Life<Curve>::Ref f_;
};
std::shared_ptr<nf7::Node::Lambda> Curve::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Curve::NodeLambda>(*this, parent);
}


class Curve::SeqLambda final : public nf7::Sequencer::Lambda {
 public:
  SeqLambda(Curve& f, const std::shared_ptr<nf7::Context>& parent) noexcept :
      nf7::Sequencer::Lambda(f, parent), f_(f.life_) {
  }

  void Run(const std::shared_ptr<nf7::Sequencer::Session>& ss) noexcept {
    try {
      ss->Send("y", nf7::Value {f_->Calc(ss->ReceiveOrThrow("x").scalar())});
    } catch (nf7::Exception&) {
    }
    ss->Finish();
  }

 private:
  nf7::Life<Curve>::Ref f_;
};
std::shared_ptr<nf7::Sequencer::Lambda> Curve::CreateLambda(
    const std::shared_ptr<nf7::Context>& parent) noexcept {
  return std::make_shared<Curve::SeqLambda>(*this, parent);
}


void Curve::UpdateItem(nf7::Sequencer::Editor&) noexcept {
  ImGui::TextUnformatted("Value/Curve");

  const auto pad = ImGui::GetStyle().WindowPadding / 2;
  ImGui::SetCursorPos(pad);
  UpdateCurveEditor(ImGui::GetContentRegionAvail()-pad);
}
void Curve::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();
  ImGui::TextUnformatted("Value/Curve");

  if (ImNodes::BeginInputSlot("x", 1)) {
    ImGui::AlignTextToFramePadding();
    nf7::gui::NodeSocket();
    ImNodes::EndSlot();
  }
  ImGui::SameLine();
  UpdateCurveEditorWindow({16*em, 6*em});
  ImGui::SameLine();
  if (ImNodes::BeginOutputSlot("y", 1)) {
    ImGui::AlignTextToFramePadding();
    nf7::gui::NodeSocket();
    ImNodes::EndSlot();
  }
}
void Curve::UpdateParamPanel(nf7::Sequencer::Editor&) noexcept {
  if (ImGui::CollapsingHeader("Value/Curve", ImGuiTreeNodeFlags_DefaultOpen)) {
    const auto em = ImGui::GetFontSize();
    UpdateCurveEditorWindow({0, 6*em});
  }
}
void Curve::UpdateWidget() noexcept {
  const auto em = ImGui::GetFontSize();
  ImGui::TextUnformatted("Value/Curve");
  UpdateCurveEditorWindow({24*em, 8*em});
}
void Curve::UpdateCurveEditorWindow(const ImVec2& size) noexcept {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {0, 0});
  const auto shown =
      ImGui::BeginChild("CurveEditor", size, true, ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar(1);
  if (shown) {
    const auto pad = ImGui::GetStyle().WindowPadding / 2;
    ImGui::SetCursorPos(pad);
    UpdateCurveEditor(ImGui::GetContentRegionAvail()-pad*2);
  }
  ImGui::EndChild();
}
void Curve::UpdateCurveEditor(const ImVec2& sz) noexcept {
  const auto& io = ImGui::GetIO();

  auto d = ImGui::GetWindowDrawList();

  const auto em   = ImGui::GetFontSize();
  const auto col  = ImGui::GetColorU32(ImGuiCol_Text);
  const auto colg = ImGui::GetColorU32(ImGuiCol_Text, .5f);
  const auto cols = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
  const auto pos  = ImGui::GetCursorScreenPos();
  const auto pad  = ImGui::GetCursorPos();
  const auto grip = em/2.4f;

  const auto mpos  = ImGui::GetMousePos() - pos;
  const auto mposn = ImVec2 {
    std::clamp(mpos.x/sz.x, 0.f, 1.f),
    std::clamp(1-mpos.y/sz.y, 0.f, 1.f),
  };

  // draw lines
  auto& terms = mem_.data().terms;
  for (size_t i = 0; i+1 < terms.size(); ++i) {
    const auto& a = terms[i];
    const auto& b = terms[i+1];

    const auto p1 = ImVec2 {sz.x*a.p1.x, sz.y*(1-a.p1.y)};
    const auto p2 = ImVec2 {sz.x*a.p2.x, sz.y*(1-a.p2.y)};
    const auto p3 = ImVec2 {sz.x*a.p3.x, sz.y*(1-a.p3.y)};
    const auto p4 = ImVec2 {sz.x*b.p1.x, sz.y*(1-b.p1.y)};
    d->AddBezierCubic(pos+p1, pos+p2, pos+p3, pos+p4, col, 1);
  }

  // draw points
  bool request_sort    = false;
  bool skip_adding     = false;
  bool remove_selected = false;
  bool reset_controls  = false;
  for (size_t i = 0; i < terms.size(); ++i) {
    auto& t  = terms[i];
    auto  pt = i >= 1? &terms[i-1]: nullptr;
    auto  nt = i+1 < terms.size()? &terms[i+1]: nullptr;

    const bool sel = selected_.contains(t.id);
    ImGui::PushID(static_cast<int>(t.id));

    const auto x = std::clamp(sz.x*t.p1.x, 1.f, sz.x-1);
    const auto y = std::clamp(sz.y*(1-t.p1.y), 1.f, sz.y-1);

    const auto p1 = ImVec2 {x, y};
    d->AddCircleFilled(pos+p1, grip, col);
    if (sel) {
      d->AddCircleFilled(pos+p1, grip, cols);
    }

    ImGui::SetCursorPos(p1 - ImVec2 {grip, grip} + pad);
    if (!io.KeyShift) {
      ImGui::InvisibleButton("grip", {grip*2, grip*2});
      if (ImGui::IsItemActive()) {
        if (ImGui::IsItemActivated()) {
          SelectPoint(t.id);
          last_action_moved_ = false;
        }
        request_sort = true;
        skip_adding  = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
          MovePoint(mposn-t.p1);
          last_action_moved_ = true;
        }
      } else {
        if (ImGui::IsItemDeactivated() && last_action_moved_) {
          mem_.Commit();
        }
        if (ImGui::IsItemHovered()) {
          skip_adding = true;
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
      }
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::IsWindowAppearing()) {
          SelectPoint(t.id);
        }
        if (ImGui::MenuItem("remove points")) {
          remove_selected = true;
        }
        if (ImGui::MenuItem("reset control points")) {
          reset_controls = true;
        }
        ImGui::EndPopup();
      }
    }

    // define control point handler
    const auto HandleControlPoint = [&](ImVec2& p, float xmin, float xmax) {
      bool ret = false;
      if (ImGui::IsItemActive()) {
        if (ImGui::IsItemActivated()) {
          last_action_moved_ = false;
        }
        skip_adding = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
          p   = mposn;
          p.x = std::clamp(p.x, xmin, xmax);
          p.y = std::clamp(p.y, 0.f, 1.f);
          last_action_moved_ = true;
        }
        ret = true;
      } else {
        if (ImGui::IsItemHovered()) {
          skip_adding = true;
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
      }
      return ret;
    };

    // p2 control point
    if (ImGui::IsWindowFocused() && io.KeyShift && nt) {
      const auto p2 = ImVec2 {sz.x*t.p2.x, sz.y*(1-t.p2.y)};
      ImGui::SetCursorPos(p2 - ImVec2 {grip, grip} + pad);
      ImGui::InvisibleButton("grip-p2", {grip*2, grip*2});
      if (HandleControlPoint(t.p2, t.p1.x, nt->p1.x)) {
        if (!t.break_prev) {
          // TODO calc reversal vector
        }
      }
      if (ImGui::IsItemDeactivated() && last_action_moved_) {
        mem_.Commit();
      }
      d->AddLine(p1+pos, p2+pos, colg);
      d->AddCircleFilled(pos+p2, grip, colg);
    }

    // prev term's p3 control point
    if (ImGui::IsWindowFocused() && io.KeyShift && pt) {
      const auto p3 = ImVec2 {sz.x*pt->p3.x, sz.y*(1-pt->p3.y)};
      ImGui::SetCursorPos(p3 - ImVec2 {grip, grip} + pad);
      ImGui::InvisibleButton("grip-p3", {grip*2, grip*2});
      if (HandleControlPoint(pt->p3, pt->p1.x, t.p1.x)) {
        if (!t.break_prev && nt) {
          // TODO calc reversal vector
        }
      }
      if (ImGui::IsItemDeactivated() && last_action_moved_) {
        mem_.Commit();
      }
      d->AddLine(p1+pos, p3+pos, colg);
      d->AddCircleFilled(pos+p3, grip, colg);
    }

    ImGui::PopID();
  }
  if (request_sort) {
    Sanitize();
  }
  if (remove_selected) {
    RemoveSelectedPoints();
  }
  if (reset_controls) {
    ResetControlsOfSelectedPoints();
  }

  // add new point
  if (!skip_adding) {
    ImGui::PushID(static_cast<int>(next_id_));
    const auto y    = static_cast<float>(Calc(mposn.x));
    const auto diff = y - mposn.y;
    if (std::abs(diff * sz.y) < grip) {
      ImGui::SetCursorPos(mpos-ImVec2 {grip/2, grip/2} + pad);
      ImGui::InvisibleButton("grip", {grip, grip});
      if (ImGui::IsItemActivated()) {
        SelectPoint(next_id_);
        AddPoint({mposn.x, y});
      }
      d->AddCircle(ImVec2 {mpos.x, sz.y*(1-y)} + pos, grip, col);
    }
    ImGui::PopID();
  }
}

}
}  // namespace nf7
