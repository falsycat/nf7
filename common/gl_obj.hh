#pragma once

#include <cassert>
#include <memory>
#include <utility>

#include <GL/glew.h>

#include "nf7.hh"

#include "common/factory.hh"
#include "common/future.hh"
#include "common/mutex.hh"


namespace nf7::gl {

template <typename T>
class Obj final {
 public:
  Obj() = delete;
  template <typename... Args>
  Obj(const std::shared_ptr<nf7::Context>& ctx, GLuint id, Args&&... args) noexcept :
      ctx_(ctx), id_(id? id: T::Gen()), meta_(std::forward<Args>(args)...) {
  }
  ~Obj() noexcept {
    ctx_->env().ExecGL(ctx_, [id = id_]() { T::Delete(id); });
  }
  Obj(const Obj&) = delete;
  Obj(Obj&&) = delete;
  Obj& operator=(const Obj&) = delete;
  Obj& operator=(Obj&&) = delete;

  GLuint id() const noexcept { return id_; }

  T& meta() noexcept { return meta_; }
  const T& meta() const noexcept { return meta_; }

 private:
  std::shared_ptr<nf7::Context> ctx_;

  GLuint id_;
  T meta_;
};


struct Obj_BufferMeta final {
 public:
  Obj_BufferMeta() = delete;
  Obj_BufferMeta(GLenum t) noexcept : type(t) {
  }

  const GLenum type;

  size_t size = 0;

  static GLuint Gen() noexcept {
    GLuint id;
    glGenBuffers(1, &id);
    return id;
  }
  static void Delete(GLuint id) noexcept {
    glDeleteBuffers(1, &id);
  }
};
using Buffer        = Obj<Obj_BufferMeta>;
using BufferFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Buffer>>>;


struct Obj_TextureMeta final {
 public:
  Obj_TextureMeta() = delete;
  Obj_TextureMeta(GLenum t) noexcept : type(t) {
  }

  const GLenum type;

  GLint format = 0;
  uint32_t w = 0, h = 0, d = 0;

  static GLuint Gen() noexcept {
    GLuint id;
    glGenTextures(1, &id);
    return id;
  }
  static void Delete(GLuint id) noexcept {
    glDeleteTextures(1, &id);
  }
};
using Texture        = Obj<Obj_TextureMeta>;
using TextureFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Texture>>>;

}  // namespace nf7::gl
