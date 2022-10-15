#include "common/gl_obj.hh"

#include <algorithm>
#include <exception>
#include <memory>
#include <vector>

#include "common/aggregate_promise.hh"
#include "common/future.hh"
#include "common/mutex.hh"


namespace nf7::gl {

nf7::Future<std::vector<std::shared_ptr<nf7::Mutex::Lock>>>
    Obj_VertexArrayMeta::LockBuffers(
        const std::shared_ptr<nf7::Context>& ctx, size_t vcnt, size_t icnt) const noexcept
try {
  std::vector<nf7::gl::BufferFactory::Product> fus;

  nf7::AggregatePromise lock_pro {ctx};
  for (const auto& attr : attrs) {
    auto& f = ctx->env().GetFileOrThrow(attr.id);

    // calculate size required to the buffer
    const auto required = std::max(attr.size_per_vertex*vcnt, attr.size_per_instance*icnt);

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
    lock_pro.Add(pro.future());
    fus.emplace_back(pro.future());
  }

  // wait for all registered futures
  nf7::Future<std::vector<std::shared_ptr<nf7::Mutex::Lock>>>::Promise pro {ctx};
  lock_pro.future().Chain(pro, [fus = std::move(fus)](auto&) {
    std::vector<std::shared_ptr<nf7::Mutex::Lock>> ret;
    for (auto& fu : fus) {
      ret.emplace_back(fu.value().lock());
    }
    return ret;
  });
  return pro.future();
} catch (nf7::Exception&) {
  return { std::current_exception() };
}

}  // namespace nf7::gl
