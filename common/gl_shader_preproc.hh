#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

#include "nf7.hh"

#include "common/future.hh"


namespace nf7::gl {

class ShaderPreproc final : public nf7::Context,
    public std::enable_shared_from_this<ShaderPreproc> {
 public:
  ShaderPreproc() = delete;
  ShaderPreproc(const std::shared_ptr<nf7::Context>& ctx,
                const std::shared_ptr<std::ostream>& ost,
                const std::shared_ptr<std::istream>& ist,
                std::filesystem::path&& path) noexcept :
      nf7::Context(ctx->env(), ctx->initiator(), ctx),
      pro_(ctx), ost_(ost), ist_(ist), path_(std::move(path)) {
  }

  ShaderPreproc(const ShaderPreproc&) = delete;
  ShaderPreproc(ShaderPreproc&&) = delete;
  ShaderPreproc& operator=(const ShaderPreproc&) = delete;
  ShaderPreproc& operator=(ShaderPreproc&&) = delete;

  void ExecProcess() noexcept {
    env().ExecAsync(shared_from_this(), [this]() { Process(); });
  }

  nf7::Future<std::monostate> future() noexcept {
    return pro_.future();
  }
  const std::vector<std::filesystem::path>& nfiles() const noexcept {
    return *nfiles_;
  }

 private:
  nf7::Future<std::monostate>::Promise pro_;

  std::shared_ptr<std::ostream> ost_;
  std::shared_ptr<std::istream> ist_;

  std::filesystem::path path_;
  size_t                lnum_ = 1;

  std::shared_ptr<std::vector<std::filesystem::path>> nfiles_;


  void Process() noexcept
  try {
    *ost_ << "#line " << lnum_ << " \"" << path_.string() << "\"\n";

    std::string line;
    while (std::getline(*ist_, line)) {
      ++lnum_;

      if (line.starts_with('#')) {
        std::string_view tok {line.begin() + 1, line.end()};
        while (!tok.empty() && !std::isalpha(tok.front())) {
          tok.remove_prefix(1);
        }
        if (tok.starts_with("include ")) {
          tok.remove_prefix(sizeof("include")-1);

          auto begin = std::find(tok.begin(), tok.end(), '"');
          auto end   = std::find(begin+1,     tok.end(), '"');
          if (begin == end || end == tok.end()) {
            throw nf7::Exception {"invalid include syntax: "+line};
          }
          if (depth() >= 100) {
            throw nf7::Exception {
              "recursion detected in include directives ("+path_.string()+")"};
          }

          const std::string name {begin+1, end};
          const auto        path = path_.parent_path() / name;

          if (nfiles_ == nullptr) {
            nfiles_ = std::make_shared<std::vector<std::filesystem::path>>();
          }
          nfiles_->push_back(path);

          auto self = shared_from_this();
          auto f    = std::make_shared<std::ifstream>(path, std::ios::binary);
          if (!*f) {
            throw nf7::Exception {"missing include file: "+path.string()};
          }

          auto sub = std::make_shared<ShaderPreproc>(self, ost_, f, path.string());
          sub->nfiles_ = nfiles_;
          sub->Process();
          sub->future().Chain(nf7::Env::kAsync, self, pro_,
                              [=, this](auto&) mutable { Process(); });
          return;
        }
      }
      *ost_ << line << "\n";
    }
    pro_.Return({});
  } catch (...) {
    pro_.Throw<nf7::Exception>("failed to preprocess GLSL");
  }
};

}  // namespace nf7::gl
