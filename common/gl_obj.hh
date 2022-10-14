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
  template <typename... Args>
  Obj(const std::shared_ptr<nf7::Context>& ctx, GLuint id, Args&&... args) noexcept :
      ctx_(ctx), meta_(std::forward<Args>(args)...), id_(id? id: meta_.Gen()) {
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

  T meta_;
  const GLuint id_;
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


struct Obj_ShaderMeta final {
 public:
  Obj_ShaderMeta() = delete;
  Obj_ShaderMeta(GLenum t) noexcept : type(t) {
  }

  const GLenum type;

  GLuint Gen() noexcept {
    return glCreateShader(type);
  }
  static void Delete(GLuint id) noexcept {
    glDeleteShader(id);
  }
};
using Shader = Obj<Obj_ShaderMeta>;
using ShaderFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Shader>>>;


struct Obj_ProgramMeta final {
 public:
  Obj_ProgramMeta() = default;

  GLuint Gen() noexcept {
    return glCreateProgram();
  }
  static void Delete(GLuint id) noexcept {
    glDeleteProgram(id);
  }
};
using Program = Obj<Obj_ProgramMeta>;
using ProgramFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Program>>>;

}  // namespace nf7::gl
