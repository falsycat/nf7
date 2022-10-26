#pragma once

#include <chrono>
#include <memory>

#include <GL/glew.h>

#include "nf7.hh"

#include "common/future.hh"


namespace nf7::gl {

inline void Await(const std::shared_ptr<nf7::Context>&  ctx,
                  nf7::Future<std::monostate>::Promise& pro,
                  GLsync                                sync) noexcept {
  const auto state = glClientWaitSync(sync, 0, 0);
  assert(0 == glGetError());

  if (state == GL_ALREADY_SIGNALED || state == GL_CONDITION_SATISFIED) {
    glDeleteSync(sync);
    pro.Return({});
  } else {
    ctx->env().ExecGL(ctx, [ctx, pro, sync]() mutable {
      Await(ctx, pro, sync);
    }, nf7::Env::Clock::now() + std::chrono::milliseconds(10));
  }
}

// The returned future will be finalized on GL thread.
inline nf7::Future<std::monostate> ExecFenceSync(
    const std::shared_ptr<nf7::Context>& ctx) noexcept {
  nf7::Future<std::monostate>::Promise pro {ctx};

  ctx->env().ExecGL(ctx, [ctx, pro]() mutable {
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    assert(0 == glGetError());
    Await(ctx, pro, sync);
  });
  return pro.future();
}

}  // namespace nf7::gl
