#pragma once

#include <array>
#include <cassert>
#include <memory>
#include <optional>
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
  using Meta  = T;
  using Param = typename Meta::Param;

  Obj(const std::shared_ptr<nf7::Context>& ctx, GLuint id, const Meta& meta) noexcept :
      ctx_(ctx), id_(id), meta_(meta) {
  }
  ~Obj() noexcept {
    ctx_->env().ExecGL(ctx_, [id = id_]() { T::Delete(id); });
  }
  Obj(const Obj&) = delete;
  Obj(Obj&&) = delete;
  Obj& operator=(const Obj&) = delete;
  Obj& operator=(Obj&&) = delete;

  GLuint id() const noexcept { return id_; }
  const Meta& meta() const noexcept { return meta_; }

  Param& param() noexcept { return param_; }
  const Param& param() const noexcept { return param_; }

 private:
  std::shared_ptr<nf7::Context> ctx_;

  const GLuint id_;
  const Meta   meta_;

  Param param_;
};


struct Obj_BufferMeta final {
 public:
  struct Param { size_t size = 0; };

  static void Delete(GLuint id) noexcept {
    glDeleteBuffers(1, &id);
  }

  // must be called from main or sub task
  nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  gl::BufferTarget target;
};
using Buffer        = Obj<Obj_BufferMeta>;
using BufferFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Buffer>>>;


struct Obj_TextureMeta final {
 public:
  struct Param { };

  static void Delete(GLuint id) noexcept {
    glDeleteTextures(1, &id);
  }

  // must be called from main or sub task
  nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  gl::TextureTarget      target;
  gl::InternalFormat     format;
  std::array<GLsizei, 3> size;
};
using Texture        = Obj<Obj_TextureMeta>;
using TextureFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Texture>>>;


struct Obj_ShaderMeta final {
 public:
  struct Param { };

  static void Delete(GLuint id) noexcept {
    glDeleteShader(id);
  }

  // must be called from main or sub task
  nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      const std::string&                   src) const noexcept;

  gl::ShaderType type;
};
using Shader = Obj<Obj_ShaderMeta>;
using ShaderFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Shader>>>;


struct Obj_ProgramMeta final {
 public:
  struct Param { };

  static void Delete(GLuint id) noexcept {
    glDeleteProgram(id);
  }

  // must be called from main or sub task
  nf7::Future<std::shared_ptr<Obj<Obj_ProgramMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      const std::vector<nf7::File::Id>&    shaders) noexcept;
};
using Program = Obj<Obj_ProgramMeta>;
using ProgramFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Program>>>;


struct Obj_VertexArrayMeta final {
 public:
  struct Param { };

  using LockedBuffersFuture =
      nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>>;

  struct Index {
    nf7::File::Id   buffer;
    gl::NumericType numtype;
  };
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

  struct ValidationHint {
    ValidationHint() noexcept { }

    size_t vertices  = 0;
    size_t instances = 0;
  };

  static void Delete(GLuint id) noexcept {
    glDeleteVertexArrays(1, &id);
  }

  // must be called from main or sub task
  nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  // must be called from main or sub task
  // it's guaranteed that the last element of the returned vector is an index buffer if index != std::nullopt
  LockedBuffersFuture LockBuffers(
      const std::shared_ptr<nf7::Context>& ctx,
      const ValidationHint&                vhint = {}) const noexcept;

  std::optional<Index> index;
  std::vector<Attr>    attrs;
};
using VertexArray = Obj<Obj_VertexArrayMeta>;
using VertexArrayFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<VertexArray>>>;


struct Obj_FramebufferMeta final {
 public:
  static constexpr size_t kColorSlotCount = 8;

  struct Param { };

  struct Attachment {
    nf7::File::Id tex;
  };

  struct LockedAttachments {
   private:
    using TexRes = nf7::Mutex::Resource<std::shared_ptr<gl::Texture>>;
   public:
    std::array<std::optional<TexRes>, kColorSlotCount> colors;
    std::optional<TexRes>                              depth;
    std::optional<TexRes>                              stencil;
  };
  using LockedAttachmentsFuture = nf7::Future<LockedAttachments>;

  static void Delete(GLuint id) noexcept {
    glDeleteFramebuffers(1, &id);
  }

  // must be called from main or sub task
  nf7::Future<std::shared_ptr<Obj<Obj_FramebufferMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  LockedAttachmentsFuture LockAttachments(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  std::array<std::optional<Attachment>, kColorSlotCount> colors;
  std::optional<Attachment>                              depth;
  std::optional<Attachment>                              stencil;
};
using Framebuffer = Obj<Obj_FramebufferMeta>;
using FramebufferFactory = AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Framebuffer>>>;

}  // namespace nf7::gl
