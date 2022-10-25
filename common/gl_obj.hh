#pragma once

#include <array>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#include <GL/glew.h>

#include "nf7.hh"

#include "common/factory.hh"
#include "common/future.hh"
#include "common/gl_enum.hh"
#include "common/mutex.hh"


namespace nf7::gl {

template <typename T>
class Obj final {
 public:
  using Meta = T;

  // must be called from main or sub task
  template <typename... Args>
  static nf7::Future<std::shared_ptr<Obj<T>>> Create(Args&&... args) noexcept {
    return Meta::Create(std::forward<Args>(args)...);
  }

  template <typename... Args>
  Obj(const std::shared_ptr<nf7::Context>& ctx, GLuint id, Args&&... args) noexcept :
      ctx_(ctx), meta_(std::forward<Args>(args)...), id_(id) {
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
  // must be called from main or sub task
  static nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx, gl::BufferTarget target) noexcept;

  static void Delete(GLuint id) noexcept {
    glDeleteBuffers(1, &id);
  }

  Obj_BufferMeta() = delete;
  Obj_BufferMeta(gl::BufferTarget t) noexcept : target(t) {
  }

  const gl::BufferTarget target;

  size_t size = 0;
};
using Buffer        = Obj<Obj_BufferMeta>;
using BufferFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Buffer>>>;


struct Obj_TextureMeta final {
 public:
  // must be called from main or sub task
  static nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      gl::TextureTarget target, GLint fmt, std::array<GLsizei, 3> size) noexcept;

  static void Delete(GLuint id) noexcept {
    glDeleteTextures(1, &id);
  }

  Obj_TextureMeta() = delete;
  Obj_TextureMeta(gl::TextureTarget t, GLint f, std::array<GLsizei, 3> s) noexcept :
      target(t), format(f), size(s) {
  }

  const gl::TextureTarget      target;
  const GLint                  format;
  const std::array<GLsizei, 3> size;
};
using Texture        = Obj<Obj_TextureMeta>;
using TextureFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Texture>>>;


struct Obj_ShaderMeta final {
 public:
  // must be called from main or sub task
  static nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      gl::ShaderType                       type,
      const std::string&                   src) noexcept;

  static void Delete(GLuint id) noexcept {
    glDeleteShader(id);
  }

  Obj_ShaderMeta() = delete;
  Obj_ShaderMeta(gl::ShaderType t) noexcept : type(t) {
  }

  const gl::ShaderType type;
};
using Shader = Obj<Obj_ShaderMeta>;
using ShaderFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Shader>>>;


struct Obj_ProgramMeta final {
 public:
  // must be called from main or sub task
  static nf7::Future<std::shared_ptr<Obj<Obj_ProgramMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      const std::vector<nf7::File::Id>&    shaders) noexcept;

  static void Delete(GLuint id) noexcept {
    glDeleteProgram(id);
  }

  Obj_ProgramMeta() = default;
};
using Program = Obj<Obj_ProgramMeta>;
using ProgramFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Program>>>;


struct Obj_VertexArrayMeta final {
 public:
  using LockedBuffersFuture =
      nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>>;

  struct Attr {
    nf7::File::Id   buffer;
    GLuint          location;
    GLint           size;
    gl::NumericType type;
    bool            normalize;
    GLsizei         stride;
    uint64_t        offset;
    GLuint          divisor;
  };

  // must be called from main or sub task
  static nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,std::vector<Attr>&& attrs) noexcept;

  static void Delete(GLuint id) noexcept {
    glDeleteVertexArrays(1, &id);
  }

  // must be called from main or sub task
  static LockedBuffersFuture
      LockBuffers(
          const std::shared_ptr<nf7::Context>& ctx,
          const std::vector<Attr>&             attrs,
          size_t                               vcnt = 0,
          size_t                               icnt = 0) noexcept;

  Obj_VertexArrayMeta(std::vector<Attr>&& a) noexcept : attrs(std::move(a)) {
  }

  nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>> LockBuffers(
      const std::shared_ptr<nf7::Context>& ctx, size_t vcnt = 0, size_t icnt = 0) const noexcept {
    return LockBuffers(ctx, attrs, vcnt, icnt);
  }

  const std::vector<Attr> attrs;
};
using VertexArray = Obj<Obj_VertexArrayMeta>;
using VertexArrayFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<VertexArray>>>;


struct Obj_FramebufferMeta final {
 public:
  using LockedAttachmentsFuture =
      nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Texture>>>>;

  struct Attachment {
    nf7::File::Id       tex;
    gl::FramebufferSlot slot;
  };

  // must be called from main or sub task
  static nf7::Future<std::shared_ptr<Obj<Obj_FramebufferMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      std::vector<Attachment>&&            attachments) noexcept;

  static void Delete(GLuint id) noexcept {
    glDeleteFramebuffers(1, &id);
  }

  // must be called from main or sub task
  static LockedAttachmentsFuture LockAttachments(
      const std::shared_ptr<nf7::Context>& ctx,
      std::span<const Attachment>          attachments) noexcept;

  // must be called on GL thread
  static void ThrowStatus(GLenum status);

  Obj_FramebufferMeta(std::vector<Attachment>&& a) noexcept : attachments(std::move(a)) {
  }

  nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Texture>>>> LockAttachments(
      const std::shared_ptr<nf7::Context>& ctx) noexcept {
    return LockAttachments(ctx, attachments);
  }

  const std::vector<Attachment> attachments;
};
using Framebuffer = Obj<Obj_FramebufferMeta>;
using FramebufferFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Framebuffer>>>;

}  // namespace nf7::gl
