#pragma once

#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "nf7.hh"


namespace nf7::gui::dnd {

// data entity is char[] of file path
constexpr const char* kFilePath = "nf7::File::Path";


template <typename T>
bool Send(const char* type, const T&) noexcept;

template <>
inline bool Send<std::string>(const char* type, const std::string& v) noexcept {
  return ImGui::SetDragDropPayload(type, v.data(), v.size());
}
template <>
inline bool Send<std::string_view>(const char* type, const std::string_view& v) noexcept {
  return ImGui::SetDragDropPayload(type, v.data(), v.size());
}
template <>
inline bool Send<File::Path>(const char* type, const File::Path& p) noexcept {
  return Send(type, p.Stringify());
}


template <typename T>
T To(const ImGuiPayload&) noexcept;

template <>
inline std::string To<std::string>(const ImGuiPayload& pay) noexcept {
  std::string ret;
  ret.resize(static_cast<size_t>(pay.DataSize));
  std::memcpy(ret.data(), pay.Data, ret.size());
  return ret;
}
template <>
inline File::Path To<File::Path>(const ImGuiPayload& pay) noexcept {
  return File::Path::Parse(To<std::string>(pay));
}


template <typename T>
std::optional<T> Accept(const char* type, ImGuiDragDropFlags flags = 0) noexcept {
  if (auto pay = ImGui::AcceptDragDropPayload(type, flags)) {
    return To<T>(*pay);
  }
  return std::nullopt;
}
template <typename T>
const ImGuiPayload* Peek(const char* type, auto& v, ImGuiDragDropFlags flags = 0) noexcept {
  flags |= ImGuiDragDropFlags_AcceptPeekOnly;
  if (auto pay = ImGui::AcceptDragDropPayload(type, flags)) {
    v = To<T>(*pay);
    return pay;
  }
  return nullptr;
}

inline bool IsFirstAccept() noexcept {
    const auto ctx = ImGui::GetCurrentContext();
    return ctx->DragDropAcceptFrameCount < ctx->FrameCount;
}

inline void DrawRect() noexcept {
  auto& r = ImGui::GetCurrentContext()->DragDropTargetRect;
  ImGui::GetForegroundDrawList()->AddRect(
      r.Min - ImVec2 {3.5f, 3.5f},
      r.Max + ImVec2 {3.5f, 3.5f},
      ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0.0f, 2.0f);
}

}  // namespace nf7::gui::dnd
