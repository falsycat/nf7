#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <string>

#include "nf7.hh"

#include "common/config.hh"


namespace nf7::gui {

// widgets
void FileMenuItems(nf7::File& f) noexcept;
void FileTooltip(nf7::File& f) noexcept;

bool PathButton(const char* id, nf7::File::Path&, nf7::File&) noexcept;
void ContextStack(const nf7::Context&) noexcept;

void NodeSocket() noexcept;
void NodeInputSockets(std::span<const std::string>) noexcept;
void NodeOutputSockets(std::span<const std::string>) noexcept;

struct ConfigEditor {
 public:
  void operator()(nf7::Config&) noexcept;

 private:
  std::string text_;
  std::string msg_;
  bool        mod_;
};


// stringify utility
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

}  // namespace nf7::gui
