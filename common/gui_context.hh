#pragma once

#include <cinttypes>
#include <string>

#include <imgui.h>

#include "nf7.hh"


namespace nf7::gui {

inline std::string GetContextDisplayName(const nf7::Context& ctx) noexcept {
  auto f = ctx.env().GetFile(ctx.initiator());

  const auto initiator =
      f? f->abspath().Stringify(): std::string {"<owner missing>"};

  char buf[32];
  std::snprintf(buf, sizeof(buf), "(0x%0" PRIXPTR ")", reinterpret_cast<uintptr_t>(&ctx));

  return initiator + " " + buf;
}

inline std::string GetParentContextDisplayName(const nf7::Context& ctx) noexcept {
  if (auto parent = ctx.parent()) {
    return nf7::gui::GetContextDisplayName(*parent);
  } else if (ctx.depth() == 0) {
    return "(isolated)";
  } else {
    return "<owner disappeared> MEMORY LEAK? ;(";
  }
}

inline void ContextStack(const nf7::Context& ctx) noexcept {
  for (auto p = ctx.parent(); p; p = p->parent()) {
    auto f = ctx.env().GetFile(p->initiator());

    const auto path = f? f->abspath().Stringify(): "[missing file]";

    ImGui::TextUnformatted(path.c_str());
    ImGui::TextDisabled("%s", p->GetDescription().c_str());
  }
}

}  // namespace nf7::gui
