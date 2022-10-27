#include "common/gl_obj.hh"

#include <algorithm>
#include <array>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

#include "common/aggregate_promise.hh"
#include "common/factory.hh"
#include "common/future.hh"
#include "common/gl_enum.hh"
#include "common/mutex.hh"


namespace nf7::gl {

template <typename T>
auto LockAndValidate(const std::shared_ptr<nf7::Context>&                         ctx,
                     nf7::AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<T>>>& factory,
                     std::function<void(const typename T::Meta&)>&&               validator) noexcept {
  typename nf7::Future<nf7::Mutex::Resource<std::shared_ptr<T>>>::Promise pro {ctx};
  factory.Create().Chain(pro, [validator](auto& v) {
        validator((*v)->meta());
        return v;
      });
  return pro.future();
}
static auto LockAndValidate(const std::shared_ptr<nf7::Context>& ctx,
                            nf7::gl::BufferFactory&              factory,
                            nf7::gl::BufferTarget                target,
                            size_t                               required) noexcept {
  return LockAndValidate<gl::Buffer>(ctx, factory, [target, required](auto& m) {
    if (m.target != target) {
      throw nf7::Exception {"incompatible buffer target"};
    }
    if (m.size < required) {
      std::stringstream st;
      st << "buffer shortage (" << m.size << "/" << required << ")";
      throw nf7::Exception {st.str()};
    }
  });
}
static auto LockAndValidate(const std::shared_ptr<nf7::Context>& ctx,
                            nf7::gl::TextureFactory&             factory,
                            nf7::gl::TextureTarget               target) noexcept {
  return LockAndValidate<gl::Texture>(ctx, factory, [target](auto& m) {
    if (m.target != target) {
      throw nf7::Exception {"incompatible texture target"};
    }
  });
}


nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>> Obj_BufferMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx, gl::BufferTarget target) noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [=]() mutable {
    GLuint id;
    glGenBuffers(1, &id);
    pro.Return(std::make_shared<Obj<Obj_BufferMeta>>(ctx, id, target));
  });
  return pro.future();
}


nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>> Obj_TextureMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx,
    gl::TextureTarget target, GLint fmt, std::array<GLsizei, 3> size) noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [=]() mutable {
    GLuint id;
    glGenTextures(1, &id);

    const auto t = gl::ToEnum(target);
    glBindTexture(t, id);
    glTexParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    switch (gl::GetDimension(target)) {
    case 2:
      glTexImage2D(t, 0, fmt, size[0], size[1], 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
      break;
    default:
      assert(false && "unknown texture target");
      break;
    }
    glBindTexture(t, 0);

    pro.Return(std::make_shared<Obj<Obj_TextureMeta>>(ctx, id, target, fmt, size));
  });
  return pro.future();
}


nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>> Obj_ShaderMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx,
    gl::ShaderType                       type,
    const std::string&                   src) noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [=]() mutable {
    const auto t  = gl::ToEnum(type);
    const auto id = glCreateShader(t);
    if (id == 0) {
      pro.Throw<nf7::Exception>("failed to allocate new shader");
      return;
    }

    static const char* kHeader =
        "#version 330\n"
        "#extension GL_ARB_shading_language_include: require\n";

    const GLchar* str[] = {kHeader, src.c_str()};
    glShaderSource(id, 2, str, nullptr);
    glCompileShader(id);
    assert(0 == glGetError());

    GLint status;
    glGetShaderiv(id, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) {
      pro.Return(std::make_shared<Obj<Obj_ShaderMeta>>(ctx, id, type));
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
        interfaceOrThrow<nf7::gl::ShaderFactory>().Create());
    apro.Add(shs.back());
  }

  nf7::Future<std::shared_ptr<Obj<Obj_ProgramMeta>>>::Promise pro {ctx};
  apro.future().Chain(nf7::Env::kGL, ctx, pro, [ctx, shs = std::move(shs)](auto&) {
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
    glLinkProgram(id);

    // check status
    GLint status;
    glGetProgramiv(id, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
      return std::make_shared<Obj<Obj_ProgramMeta>>(ctx, id);
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


nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>> Obj_VertexArrayMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx,
    const std::optional<Index>&          index,
    std::vector<Attr>&&                  attrs) noexcept
try {
  if (index) {
    if (index->numtype != gl::NumericType::U8 &&
        index->numtype != gl::NumericType::U16 &&
        index->numtype != gl::NumericType::U32) {
      throw nf7::Exception {"invalid index buffer numtype (only u8/u16/u32 are allowed)"};
    }
  }

  nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>>::Promise pro {ctx};
  LockBuffers(ctx, index, attrs).Chain(
      nf7::Env::kGL, ctx, pro,
      [ctx, index, attrs = std::move(attrs), pro](auto& bufs) mutable {
        // check all buffers
        if (index) {
          assert(bufs.size() == attrs.size()+1);
          const auto& m = (*bufs.back().value()).meta();
          if (m.target != gl::BufferTarget::ElementArray) {
            throw nf7::Exception {"index buffer is not ElementArray"};
          }
        } else {
          assert(bufs.size() == attrs.size());
        }
        for (size_t i = 0; i < attrs.size(); ++i) {
          if ((*bufs[i].value()).meta().target != gl::BufferTarget::Array) {
            throw nf7::Exception {"buffer is not Array"};
          }
        }

        GLuint id;
        glGenVertexArrays(1, &id);
        glBindVertexArray(id);
        for (size_t i = 0; i < attrs.size(); ++i) {
          const auto& attr = attrs[i];
          const auto& buf  = *bufs[i].value();

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
          const auto& buf = *bufs.back().value();
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.id());
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        assert(0 == glGetError());

        return std::make_shared<Obj<Obj_VertexArrayMeta>>(ctx, id, index, std::move(attrs));
      });
  return pro.future();
} catch (nf7::Exception&) {
  return {std::current_exception()};
}

nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>>
    Obj_VertexArrayMeta::LockBuffers(
        const std::shared_ptr<nf7::Context>& ctx,
        const std::optional<Index>&          index,
        const std::vector<Attr>&             attrs,
        const ValidationHint&                vhint) noexcept
try {
  nf7::AggregatePromise apro {ctx};

  std::vector<nf7::gl::BufferFactory::Product> fus;
  fus.reserve(1+attrs.size());

  // lock array buffers
  for (const auto& attr : attrs) {
    const size_t required =
        // when non-instanced and no-index-buffer drawing
        attr.divisor == 0 && vhint.vertices > 0 && !index?
          static_cast<size_t>(attr.size) * vhint.vertices * gl::GetByteSize(attr.type):
        // when instanced drawing
        attr.divisor > 0 && vhint.instances > 0?
          static_cast<size_t>(attr.stride) * (vhint.instances-1) + attr.offset:
        size_t {0};

    auto& factory = ctx->env().
        GetFileOrThrow(attr.buffer).
        interfaceOrThrow<gl::BufferFactory>();
    auto fu = LockAndValidate(ctx, factory, gl::BufferTarget::Array, required);
    apro.Add(fu);
    fus.emplace_back(fu);
  }

  // lock index buffers (it must be the last element in `fus`)
  if (index) {
    const auto required = gl::GetByteSize(index->numtype) * vhint.vertices;

    auto& factory = ctx->env().
        GetFileOrThrow(index->buffer).
        interfaceOrThrow<gl::BufferFactory>();
    auto fu = LockAndValidate(ctx, factory, gl::BufferTarget::ElementArray, required);
    apro.Add(fu);
    fus.emplace_back(fu);
  }

  // wait for all registered futures
  nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>>::Promise pro {ctx};
  apro.future().Chain(pro, [fus = std::move(fus)](auto&) {
    std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>> ret;
    ret.reserve(fus.size());
    for (auto& fu : fus) {
      ret.emplace_back(fu.value());
    }
    return ret;
  });
  return pro.future();
} catch (nf7::Exception&) {
  return { std::current_exception() };
}


nf7::Future<std::shared_ptr<Obj<Obj_FramebufferMeta>>> Obj_FramebufferMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx,
    std::vector<Attachment>&&            atts) noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_FramebufferMeta>>>::Promise pro {ctx};
  LockAttachments(ctx, atts).
      Chain(nf7::Env::kGL, ctx, pro, [ctx, atts = std::move(atts)](auto& texs) mutable {
        assert(atts.size() == texs.size());

        GLuint id;
        glGenFramebuffers(1, &id);

        const char* err = nullptr;
        glBindFramebuffer(GL_FRAMEBUFFER, id);
        for (size_t i = 0; i < atts.size() && !err; ++i) {
          glFramebufferTexture(GL_FRAMEBUFFER, gl::ToEnum(atts[i].slot), (*texs[i])->id(), 0);
          if (0 != glGetError()) {
            err = "failed to attach texture";
          }
        }
        const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        assert(0 == glGetError());

        const auto ret = std::make_shared<Obj<Obj_FramebufferMeta>>(ctx, id, std::move(atts));
        if (err) {
          throw nf7::Exception {err};
        }
        ThrowStatus(status);
        return ret;
      });
  return pro.future();
}

nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Texture>>>>
    Obj_FramebufferMeta::LockAttachments(
        const std::shared_ptr<nf7::Context>& ctx,
        std::span<const Attachment>          attachments) noexcept
try {
  nf7::AggregatePromise apro {ctx};
  std::vector<nf7::Future<nf7::Mutex::Resource<std::shared_ptr<gl::Texture>>>> fus;
  fus.reserve(attachments.size());

  for (const auto& attachment : attachments) {
    auto& factory = ctx->env().
        GetFileOrThrow(attachment.tex).
        interfaceOrThrow<nf7::gl::TextureFactory>();
    auto fu = LockAndValidate(ctx, factory, gl::TextureTarget::Tex2D);
    apro.Add(fu);
    fus.push_back(fu);
  }

  nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Texture>>>>::Promise pro {ctx};
  apro.future().Chain(pro, [fus = std::move(fus)](auto&) {
    std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Texture>>> ret;
    ret.reserve(fus.size());
    for (auto& fu : fus) {
      ret.emplace_back(fu.value());
    }
    return ret;
  });
  return pro.future();
} catch (nf7::Exception&) {
  return { std::current_exception() };
}

void Obj_FramebufferMeta::ThrowStatus(GLenum status) {
  switch (status) {
  case GL_FRAMEBUFFER_COMPLETE:
    return;
  case GL_FRAMEBUFFER_UNDEFINED:
    throw nf7::Exception {"no framebuffer bound"};
  case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
    throw nf7::Exception {"no framebuffer bound"};
  case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
    throw nf7::Exception {"nothing attached"};
  case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
    throw nf7::Exception {"no color attachments"};
  case GL_FRAMEBUFFER_UNSUPPORTED:
    throw nf7::Exception {"unsupported internal format"};
  case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
    throw nf7::Exception {"incomplete multisample"};
  default:
    throw nf7::Exception {"unknown framebuffer status"};
  }
}

}  // namespace nf7::gl
