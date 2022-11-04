#pragma once

#include <exception>
#include <memory>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "nf7.hh"

#include "common/font_queue.hh"
#include "common/future.hh"


namespace nf7::font {

class Face final {
 public:
  static nf7::Future<std::shared_ptr<Face>> Create(
      const std::shared_ptr<nf7::Context>&     ctx,
      const std::shared_ptr<nf7::font::Queue>& q,
      const std::filesystem::path&             path) noexcept {
    nf7::Future<std::shared_ptr<Face>>::Promise pro {ctx};
    q->Push(ctx, [=](auto ft) mutable {
      try {
        FT_Face face;
        font::Enforce(FT_New_Face(ft, path.generic_string().c_str(), 0, &face));
        pro.Return(std::make_shared<Face>(ctx, q, face));
      } catch (nf7::Exception&) {
        pro.Throw(std::current_exception());
      }
    });
    return pro.future();
  }

  Face(const std::shared_ptr<nf7::Context>&     ctx,
       const std::shared_ptr<nf7::font::Queue>& q,
       FT_Face                                  face) noexcept :
      ctx_(ctx), q_(q), face_(face) {
  }
  ~Face() noexcept {
    q_->Push(ctx_, [face = face_](auto) {
      FT_Done_Face(face);
    });
  }

  FT_Face operator*() const noexcept { return face_; }

  const std::shared_ptr<nf7::font::Queue>& ftq() const noexcept { return q_; }

 private:
  std::shared_ptr<nf7::Context> ctx_;
  std::shared_ptr<nf7::font::Queue> q_;
  FT_Face face_;
};

}  // namespace nf7::font
