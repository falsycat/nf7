#include "common/gui_window.hh"

#include <imgui.h>
#include <imgui_internal.h>


namespace nf7::gui {

bool Window::MenuItem() noexcept {
  return ImGui::MenuItem(title_.c_str(), nullptr, &shown_);
}

void Window::Handle(const nf7::File::Event& e) noexcept {
  switch (e.type) {
  case nf7::File::Event::kReqFocus:
    SetFocus();
    return;
  default:
    return;
  }
}

void Window::Update() noexcept {
  const auto idstr = id();
  auto win = ImGui::FindWindowByName(idstr.c_str());

  if (std::exchange(set_focus_, false)) {
    shown_ = true;
    ImGui::SetNextWindowFocus();

    // activate parent windows recursively
    auto node = win && win->DockNode? win->DockNode->HostWindow: nullptr;
    while (node) {
      ImGui::SetWindowFocus(node->Name);
      node = node->ParentWindow;
    }
  }
  if (!shown_) return;

  onConfig();
  if (ImGui::Begin(idstr.c_str(), &shown_)) {
    onUpdate();
  }
  ImGui::End();
}

}  // namespace nf7::gui
