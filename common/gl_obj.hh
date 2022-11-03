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
  using Meta    = T;
  using Param   = typename Meta::Param;
  using Factory = nf7::AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<Obj<T>>>>;

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

  nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  gl::BufferTarget target;
};
using Buffer = Obj<Obj_BufferMeta>;


struct Obj_TextureMeta final {
 public:
  struct Param { };

  static void Delete(GLuint id) noexcept {
    glDeleteTextures(1, &id);
  }

  nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  gl::TextureTarget      target;
  gl::InternalFormat     format;
  std::array<GLsizei, 3> size;
};
using Texture = Obj<Obj_TextureMeta>;


struct Obj_ShaderMeta final {
 public:
  struct Param { };

  static void Delete(GLuint id) noexcept {
    glDeleteShader(id);
  }

  nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      const std::string&                   src) const noexcept;

  gl::ShaderType type;
};
using Shader = Obj<Obj_ShaderMeta>;


struct Obj_ProgramMeta final {
 public:
  struct Param { };

  struct Depth {
    float near = 0, far = 1;
    gl::TestFunc func = gl::TestFunc::Less;

    void serialize(auto& ar) { ar(near, far, func); }
  };

  static void Delete(GLuint id) noexcept {
    glDeleteProgram(id);
  }

  nf7::Future<std::shared_ptr<Obj<Obj_ProgramMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx,
      const std::vector<nf7::File::Id>&    shaders) noexcept;

  void ApplyState() const noexcept;
  void RevertState() const noexcept;

  std::optional<Depth> depth;
};
using Program = Obj<Obj_ProgramMeta>;


struct Obj_VertexArrayMeta final {
 public:
  struct Param { };

  struct LockedAttachments {
    std::optional<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>> index;
    std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>   attrs;
  };
  using LockedAttachmentsFuture = nf7::Future<LockedAttachments>;

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
    size_t vertices  = 0;
    size_t instances = 0;
  };

  static void Delete(GLuint id) noexcept {
    glDeleteVertexArrays(1, &id);
  }

  nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  // it's guaranteed that the last element of the returned vector is an index buffer if index != std::nullopt
  LockedAttachmentsFuture LockAttachments(
      const std::shared_ptr<nf7::Context>& ctx,
      const ValidationHint&                vhint = {0, 0}) const noexcept;

  std::optional<Index> index;
  std::vector<Attr>    attrs;
};
using VertexArray = Obj<Obj_VertexArrayMeta>;


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

  nf7::Future<std::shared_ptr<Obj<Obj_FramebufferMeta>>> Create(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  LockedAttachmentsFuture LockAttachments(
      const std::shared_ptr<nf7::Context>& ctx) const noexcept;

  std::array<std::optional<Attachment>, kColorSlotCount> colors;
  std::optional<Attachment>                              depth;
  std::optional<Attachment>                              stencil;
};
using Framebuffer = Obj<Obj_FramebufferMeta>;



// acquires locks of the object and all of its attachments
template <typename T, typename... Args>
auto LockRecursively(typename T::Factory&                 factory,
                     const std::shared_ptr<nf7::Context>& ctx,
                     Args&&...                            args) noexcept {
  typename nf7::Future<std::pair<nf7::Mutex::Resource<std::shared_ptr<T>>,
                                 typename T::Meta::LockedAttachments>>::Promise pro {ctx};
  factory.Create().Chain(pro, [=, ...args = std::forward<Args>(args)](auto& obj) mutable {
    (**obj).meta().LockAttachments(ctx, std::forward<Args>(args)...).
        Chain(pro, [obj](auto& att) {
          return std::make_pair(obj, att);
        });
  });
  return pro.future();
}

}  // namespace nf7::gl
