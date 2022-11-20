#include "common/gl_obj.hh"

#include <algorithm>
#include <array>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <tracy/Tracy.hpp>

#include "common/aggregate_promise.hh"
#include "common/factory.hh"
#include "common/future.hh"
#include "common/gl_enum.hh"
#include "common/mutex.hh"


namespace nf7::gl {

template <typename T>
auto LockAndValidate(const std::shared_ptr<nf7::Context>& ctx,
                     typename T::Factory&                 factory,
                     std::function<void(const T&)>&&      validator) noexcept {
  typename nf7::Future<nf7::Mutex::Resource<std::shared_ptr<T>>>::Promise pro {ctx};
  factory.Create().Chain(pro, [validator](auto& v) {
        validator(**v);
        return v;
      });
  return pro.future();
}
static auto LockAndValidate(const std::shared_ptr<nf7::Context>& ctx,
                            nf7::gl::Buffer::Factory&            factory,
                            nf7::gl::BufferTarget                target,
                            size_t                               required) noexcept {
  return LockAndValidate<gl::Buffer>(ctx, factory, [target, required](auto& buf) {
    if (buf.meta().target != target) {
      throw nf7::Exception {"incompatible buffer target"};
    }
    const auto size = buf.param().size;
    if (size < required) {
      std::stringstream st;
      st << "buffer shortage (" << size << "/" << required << ")";
      throw nf7::Exception {st.str()};
    }
  });
}
static auto LockAndValidate(const std::shared_ptr<nf7::Context>& ctx,
                            nf7::gl::Texture::Factory&           factory,
                            nf7::gl::TextureTarget               target) noexcept {
  return LockAndValidate<gl::Texture>(ctx, factory, [target](auto& tex) {
    if (tex.meta().target != target) {
      throw nf7::Exception {"incompatible texture target"};
    }
  });
}


nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>> Obj_BufferMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx) const noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [=, *this]() mutable {
    ZoneScopedN("create buffer");
    GLuint id;
    glGenBuffers(1, &id);
    pro.Return(std::make_shared<Obj<Obj_BufferMeta>>(ctx, id, *this));
  });
  return pro.future();
}


nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>> Obj_TextureMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx) const noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [=, *this]() mutable {
    ZoneScopedN("create texture");

    GLuint id;
    glGenTextures(1, &id);

    const auto t = gl::ToEnum(target);
    glBindTexture(t, id);
    glTexParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    const auto   ifmt = static_cast<GLint>(gl::ToEnum(format));
    const GLenum fmt  = gl::IsColor(format)? GL_RED: GL_DEPTH_COMPONENT;
    switch (gl::GetDimension(target)) {
    case 2:
      glTexImage2D(t, 0, ifmt, size[0], size[1], 0,
                   fmt, GL_UNSIGNED_BYTE, nullptr);
      break;
    default:
      assert(false && "unknown texture target");
      break;
    }
    glBindTexture(t, 0);
    assert(0 == glGetError());

    pro.Return(std::make_shared<Obj<Obj_TextureMeta>>(ctx, id, *this));
  });
  return pro.future();
}


nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>> Obj_ShaderMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx,
    const std::string&                   src) const noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [=, *this]() mutable {
    ZoneScopedN("create shader");

    const auto t  = gl::ToEnum(type);
    const auto id = glCreateShader(t);
    if (id == 0) {
      pro.Throw<nf7::Exception>("failed to allocate new shader");
      return;
    }

    static const char* kHeader =
        "#version 330\n"
        "#extension GL_ARB_shading_language_include: require\n";

    {
      ZoneScopedN("compile");
      const GLchar* str[] = {kHeader, src.c_str()};
      glShaderSource(id, 2, str, nullptr);
      glCompileShader(id);
      assert(0 == glGetError());
    }

    GLint status;
    glGetShaderiv(id, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) {
      pro.Return(std::make_shared<Obj<Obj_ShaderMeta>>(ctx, id, *this));
    } else {
      GLint len;
      glGetShaderiv(id, GL_INFO_LOG_LENGTH, &len);

      std::string ret(static_cast<size_t>(len), ' ');
      glGetShaderInfoLog(id, len, nullptr, ret.data());

      pro.Throw<nf7::Exception>(std::move(ret));
    }
  });
  return pro.future();
}


nf7::Future<std::shared_ptr<Obj<Obj_ProgramMeta>>> Obj_ProgramMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx,
    const std::vector<nf7::File::Id>&    shaders) noexcept {
  nf7::AggregatePromise apro {ctx};
  std::vector<nf7::Future<nf7::Mutex::Resource<std::shared_ptr<gl::Shader>>>> shs;
  for (auto shader : shaders) {
    shs.emplace_back(ctx->env().GetFileOrThrow(shader).
        interfaceOrThrow<nf7::gl::Shader::Factory>().Create());
    apro.Add(shs.back());
  }

  nf7::Future<std::shared_ptr<Obj<Obj_ProgramMeta>>>::Promise pro {ctx};
  apro.future().Chain(nf7::Env::kGL, ctx, pro, [*this, ctx, shs = std::move(shs)](auto&) {
    ZoneScopedN("create program");

    // check all shaders
    for (auto& sh : shs) { sh.value(); }

    // create program
    const auto id = glCreateProgram();
    if (id == 0) {
      throw nf7::Exception {"failed to allocate new program"};
    }

    // attach shaders
    for (auto& sh : shs) {
      glAttachShader(id, (*sh.value())->id());
    }

    {
      ZoneScopedN("link");
      glLinkProgram(id);
    }

    // check status
    GLint status;
    glGetProgramiv(id, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
      return std::make_shared<Obj<Obj_ProgramMeta>>(ctx, id, *this);
    } else {
      GLint len;
      glGetProgramiv(id, GL_INFO_LOG_LENGTH, &len);

      std::string ret(static_cast<size_t>(len), ' ');
      glGetProgramInfoLog(id, len, nullptr, ret.data());
      throw nf7::Exception {std::move(ret)};
    }
  });
  return pro.future();
}

void Obj_ProgramMeta::ApplyState() const noexcept {
  if (depth) {
    glEnable(GL_DEPTH_TEST);
    glDepthRange(depth->near, depth->far);
    glDepthFunc(gl::ToEnum(depth->func));
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
void Obj_ProgramMeta::RevertState() const noexcept {
  glBlendFunc(GL_ONE, GL_ZERO);
  glDisable(GL_BLEND);

  if (depth) {
    glDisable(GL_DEPTH_TEST);
  }
}


nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>> Obj_VertexArrayMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx) const noexcept
try {
  if (index) {
    if (index->numtype != gl::NumericType::U8 &&
        index->numtype != gl::NumericType::U16 &&
        index->numtype != gl::NumericType::U32) {
      throw nf7::Exception {"invalid index buffer numtype (only u8/u16/u32 are allowed)"};
    }
  }

  nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>>::Promise pro {ctx};
  LockAttachments(ctx).Chain(
      nf7::Env::kGL, ctx, pro,
      [*this, ctx, pro](auto& bufs) mutable {
        ZoneScopedN("create va");

        // check all buffers
        if (index) {
          assert(bufs.index);
          const auto& m = (***bufs.index).meta();
          if (m.target != gl::BufferTarget::ElementArray) {
            throw nf7::Exception {"index buffer is not ElementArray"};
          }
        }
        assert(bufs.attrs.size() == attrs.size());
        for (size_t i = 0; i < attrs.size(); ++i) {
          if ((**bufs.attrs[i]).meta().target != gl::BufferTarget::Array) {
            throw nf7::Exception {"buffer is not Array"};
          }
        }

        GLuint id;
        glGenVertexArrays(1, &id);
        glBindVertexArray(id);
        for (size_t i = 0; i < attrs.size(); ++i) {
          const auto& attr = attrs[i];
          const auto& buf  = **bufs.attrs[i];

          glBindBuffer(GL_ARRAY_BUFFER, buf.id());
          glEnableVertexAttribArray(attr.location);
          glVertexAttribDivisor(attr.location, attr.divisor);
          glVertexAttribPointer(
              attr.location,
              attr.size,
              gl::ToEnum(attr.type),
              attr.normalize,
              attr.stride,
              reinterpret_cast<GLvoid*>(static_cast<GLintptr>(attr.offset)));
        }
        if (index) {
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (***bufs.index).id());
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        assert(0 == glGetError());

        return std::make_shared<Obj<Obj_VertexArrayMeta>>(ctx, id, *this);
      });
  return pro.future();
} catch (nf7::Exception&) {
  return {std::current_exception()};
}

Obj_VertexArrayMeta::LockedAttachmentsFuture Obj_VertexArrayMeta::LockAttachments(
    const std::shared_ptr<nf7::Context>& ctx,
    const ValidationHint&                vhint) const noexcept
try {
  const auto Lock = [&](nf7::File::Id id, gl::BufferTarget target, size_t req) {
    auto& factory = ctx->env().
        GetFileOrThrow(id).interfaceOrThrow<gl::Buffer::Factory>();
    return LockAndValidate(ctx, factory, target, req);
  };

  nf7::AggregatePromise apro {ctx};

  auto ret = std::make_shared<LockedAttachments>();
  LockedAttachmentsFuture::Promise pro {ctx};

  // lock array buffers
  std::unordered_map<nf7::File::Id, gl::Buffer::Factory::Product> attrs_fu_map;
  for (size_t i = 0; i < attrs.size(); ++i) {
    const auto& attr = attrs[i];

    const size_t required =
        // when non-instanced and no-index-buffer drawing
        attr.divisor == 0 && vhint.vertices > 0 && !index?
          static_cast<size_t>(attr.size) * vhint.vertices * gl::GetByteSize(attr.type):
        // when instanced drawing
        attr.divisor > 0 && vhint.instances > 0?
          static_cast<size_t>(attr.stride) * (vhint.instances-1) + attr.offset:
        size_t {0};

    if (attrs_fu_map.end() == attrs_fu_map.find(attr.buffer)) {
      auto [itr, add] = attrs_fu_map.emplace(
          attr.buffer, Lock(attr.buffer, gl::BufferTarget::Array, required));
      (void) add;
      apro.Add(itr->second);
    }
  }

  // serialize attrs_fu_map
  std::vector<gl::Buffer::Factory::Product> attrs_fu;
  for (const auto& attr : attrs) {
    auto itr = attrs_fu_map.find(attr.buffer);
    assert(itr != attrs_fu_map.end());
    attrs_fu.push_back(itr->second);
  }

  // lock index buffers (it must be the last element in `fus`)
  if (index) {
    const auto required = gl::GetByteSize(index->numtype) * vhint.vertices;
    apro.Add(Lock(index->buffer, gl::BufferTarget::ElementArray, required).
        Chain(pro, [ret](auto& buf) { ret->index = buf; }));
  }

  // return ret
  apro.future().Chain(pro, [ret, attrs_fu = std::move(attrs_fu)](auto&) {
    ret->attrs.reserve(attrs_fu.size());
    for (auto& fu : attrs_fu) {
      ret->attrs.push_back(fu.value());
    }
    return std::move(*ret);
  });
  return pro.future();
} catch (nf7::Exception&) {
  return { std::current_exception() };
}


nf7::Future<std::shared_ptr<Obj<Obj_FramebufferMeta>>> Obj_FramebufferMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx) const noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_FramebufferMeta>>>::Promise pro {ctx};
  LockAttachments(ctx).
      Chain(nf7::Env::kGL, ctx, pro, [ctx, *this](auto& k) mutable {
        ZoneScopedN("create fb");

        GLuint id;
        glGenFramebuffers(1, &id);

        glBindFramebuffer(GL_FRAMEBUFFER, id);
        for (size_t i = 0; i < colors.size(); ++i) {
          if (const auto& tex = k.colors[i]) {
            glFramebufferTexture(GL_FRAMEBUFFER,
                                 static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                                 (***tex).id(),
                                 0  /* = level */);
          }
        }
        if (k.depth) {
          glFramebufferTexture(GL_FRAMEBUFFER,
                               GL_DEPTH_ATTACHMENT,
                               (***k.depth).id(),
                               0  /* = level */);
        }
        if (k.stencil) {
          glFramebufferTexture(GL_FRAMEBUFFER,
                               GL_STENCIL_ATTACHMENT,
                               (***k.stencil).id(),
                               0  /* = level */);
        }

        const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        const auto ret = std::make_shared<Obj<Obj_FramebufferMeta>>(ctx, id, *this);
        if (0 != glGetError()) {
          throw nf7::Exception {"failed to setup framebuffer"};
        }
        if (status != GL_FRAMEBUFFER_COMPLETE) {
          throw nf7::Exception {"invalid framebuffer status"};
        }
        return ret;
      });
  return pro.future();
}

Obj_FramebufferMeta::LockedAttachmentsFuture Obj_FramebufferMeta::LockAttachments(
    const std::shared_ptr<nf7::Context>& ctx) const noexcept
try {
  auto ret = std::make_shared<LockedAttachments>();

  // file duplication check for preventing deadlock by double lock
  std::unordered_set<nf7::File::Id> locked;
  for (const auto& col : colors) {
    if (col && col->tex && !locked.insert(col->tex).second) {
      throw nf7::Exception {"attached color texture is duplicated"};
    }
  }
  if (depth && depth->tex && !locked.insert(depth->tex).second) {
    throw nf7::Exception {"attached depth texture is duplicated"};
  }
  if (stencil && stencil->tex && !locked.insert(stencil->tex).second) {
    throw nf7::Exception {"attached stencil texture is duplicated"};
  }

  nf7::AggregatePromise apro {ctx};
  LockedAttachmentsFuture::Promise pro {ctx};

  const auto Lock = [&](nf7::File::Id id) {
    auto& factory = ctx->env().
        GetFileOrThrow(id).
        interfaceOrThrow<gl::Texture::Factory>();
    return LockAndValidate(ctx, factory, gl::TextureTarget::Tex2D);
  };

  for (size_t i = 0; i < colors.size(); ++i) {
    const auto& color = colors[i];
    if (color && color->tex) {
      apro.Add(Lock(color->tex).Chain(pro, [i, ret](auto& res) {
        ret->colors[i] = res;
      }));
    }
  }
  if (depth && depth->tex) {
    apro.Add(Lock(depth->tex).Chain(pro, [ret](auto& res) {
      ret->depth = res;
    }));
  }
  if (stencil && stencil->tex) {
    apro.Add(Lock(stencil->tex).Chain(pro, [ret](auto& res) {
      ret->stencil = res;
    }));
  }

  apro.future().Chain(pro, [ret](auto&) { return std::move(*ret); });
  return pro.future();
} catch (nf7::Exception&) {
  return { std::current_exception() };
}

}  // namespace nf7::gl
