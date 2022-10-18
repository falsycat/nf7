#include "common/gl_obj.hh"

#include <algorithm>
#include <exception>
#include <memory>
#include <vector>

#include "common/aggregate_promise.hh"
#include "common/future.hh"
#include "common/mutex.hh"


namespace nf7::gl {

nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>> Obj_BufferMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx, GLenum type) noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_BufferMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [ctx, type, pro]() mutable {
    GLuint id;
    glGenBuffers(1, &id);
    pro.Return(std::make_shared<Obj<Obj_BufferMeta>>(ctx, id, type));
  });
  return pro.future();
}


nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>> Obj_TextureMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx, GLenum type) noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_TextureMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [ctx, type, pro]() mutable {
    GLuint id;
    glGenTextures(1, &id);
    pro.Return(std::make_shared<Obj<Obj_TextureMeta>>(ctx, id, type));
  });
  return pro.future();
}


nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>> Obj_ShaderMeta::Create(
    const std::shared_ptr<nf7::Context>& ctx,
    GLenum                               type,
    const std::string&                   src) noexcept {
  nf7::Future<std::shared_ptr<Obj<Obj_ShaderMeta>>>::Promise pro {ctx};
  ctx->env().ExecGL(ctx, [ctx, type, src, pro]() mutable {
    const auto id = glCreateShader(type);
    if (id == 0) {
      pro.Throw<nf7::Exception>("failed to allocate new shader");
      return;
    }

    const GLchar* str = src.c_str();
    glShaderSource(id, 1, &str, nullptr);
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
    const std::shared_ptr<nf7::Context>& ctx, std::vector<Attr>&& attrs) noexcept
try {
  nf7::Future<std::shared_ptr<Obj<Obj_VertexArrayMeta>>>::Promise pro {ctx};
  LockBuffers(ctx, attrs).Chain(
      nf7::Env::kGL, ctx, pro,
      [ctx, attrs = std::move(attrs), pro](auto& bufs) mutable {
        // check all buffers
        assert(attrs.size() == bufs.size());
        for (auto& buf : bufs) {
          if ((*buf.value()).meta().type != GL_ARRAY_BUFFER) {
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
          glEnableVertexAttribArray(attr.index);
          glVertexAttribDivisor(attr.index, attr.divisor);
          glVertexAttribPointer(
              attr.index,
              attr.size,
              attr.type,
              attr.normalize,
              attr.stride,
              reinterpret_cast<GLvoid*>(static_cast<GLintptr>(attr.offset)));
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        assert(0 == glGetError());

        return std::make_shared<Obj<Obj_VertexArrayMeta>>(ctx, id, std::move(attrs));
      });
  return pro.future();
} catch (nf7::Exception&) {
  return {std::current_exception()};
}

nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>> Obj_VertexArrayMeta::LockBuffers(
    const std::shared_ptr<nf7::Context>& ctx,
    const std::vector<Attr>&             attrs,
    size_t vcnt, size_t icnt) noexcept
try {
  std::vector<nf7::gl::BufferFactory::Product> fus;

  nf7::AggregatePromise apro {ctx};
  for (const auto& attr : attrs) {
    nf7::File& f = ctx->env().GetFileOrThrow(attr.buffer);

    // calculate size required to the buffer
    size_t required = 0;
    if (attr.divisor == 0 && vcnt > 0) {
      required = static_cast<size_t>(attr.size)*vcnt;
      switch (attr.type) {
      case GL_UNSIGNED_BYTE:
      case GL_BYTE:
        required *= 1;
        break;
      case GL_UNSIGNED_SHORT:
      case GL_SHORT:
      case GL_HALF_FLOAT:
        required *= 2;
        break;
      case GL_UNSIGNED_INT:
      case GL_INT:
      case GL_FLOAT:
        required *= 4;
        break;
      case GL_DOUBLE:
        required *= 8;
        break;
      default:
        throw nf7::Exception {"unknown attribute type"};
      }
    } else if (attr.divisor > 0 && icnt > 0) {
      required = static_cast<size_t>(attr.stride)*(icnt-1) + attr.offset;
    }

    // validation after the lock
    nf7::Future<nf7::Mutex::Resource<std::shared_ptr<nf7::gl::Buffer>>>::Promise pro {ctx};
    f.interfaceOrThrow<nf7::gl::BufferFactory>().Create().
        Chain(pro, [pro, required](auto& v) mutable {
          if ((*v)->meta().size < required) {
            throw nf7::Exception {"buffer shortage"};
          }
          return v;
        });

    // register a future of the validation
    apro.Add(pro.future());
    fus.emplace_back(pro.future());
  }

  // wait for all registered futures
  nf7::Future<std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>>>::Promise pro {ctx};
  apro.future().Chain(pro, [fus = std::move(fus)](auto&) {
    std::vector<nf7::Mutex::Resource<std::shared_ptr<gl::Buffer>>> ret;
    for (auto& fu : fus) {
      ret.emplace_back(fu.value());
    }
    return ret;
  });
  return pro.future();
} catch (nf7::Exception&) {
  return { std::current_exception() };
}

}  // namespace nf7::gl
