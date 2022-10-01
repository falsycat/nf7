#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <lua.hpp>

#include "nf7.hh"

#include "common/future.hh"
#include "common/generic_context.hh"
#include "common/luajit_ref.hh"
#include "common/luajit_thread.hh"


namespace nf7::luajit {

class NFileImporter :
    public nf7::luajit::Thread::Importer,
    public std::enable_shared_from_this<NFileImporter> {
 public:
  NFileImporter(const std::filesystem::path& base) noexcept : base_(base) {
  }

  nf7::Future<std::shared_ptr<luajit::Ref>> Import(
      const luajit::Thread& th, std::string_view name) noexcept {
    auto self = shared_from_this();

    const auto path = base_ / std::string {name};

    auto ljq = th.ljq();
    auto ctx = std::make_shared<
        nf7::GenericContext>(th.env(), th.ctx()->initiator(),
                             "LuaJIT imported script (nfile)", th.ctx());
    nf7::Future<std::shared_ptr<luajit::Ref>>::Promise pro {ctx};

    // create new thread
    auto handler = luajit::Thread::CreatePromiseHandler<std::shared_ptr<luajit::Ref>>(
        pro, [self, this, path, ljq, ctx](auto L) {
      if (lua_gettop(L) <= 1) {
        AddImport(path);
        return std::make_shared<nf7::luajit::Ref>(ctx, ljq, L);
      } else {
        throw nf7::Exception {"imported script can return 1 or less results"};
      }
    });
    auto th_sub = std::make_shared<
        nf7::luajit::Thread>(ctx, ljq, std::move(handler));
    th_sub->Install(th);

    // install new importer for sub thread
    auto dir = path;
    dir.remove_filename();
    th_sub->Install(std::make_shared<NFileImporter>(dir));

    // start the thread
    ljq->Push(ctx, [pro, path, th_sub](auto L) mutable {
      L = th_sub->Init(L);
      if (0 == luaL_loadfile(L, path.string().c_str())) {
        th_sub->Resume(L, 0);
      } else {
        pro.Throw<nf7::Exception>(std::string {"import failed: "}+lua_tostring(L, -1));
      }
    });
    return pro.future();
  }

  void ClearImports() noexcept {
    std::unique_lock<std::mutex> _ {mtx_};
    imports_.clear();
  }

  std::filesystem::file_time_type GetLatestMod() const noexcept {
    std::unique_lock<std::mutex> _ {mtx_};

    std::filesystem::file_time_type ret = {};
    for (const auto& p : imports_) {
      try {
        ret = std::max(ret, std::filesystem::last_write_time(p));
      } catch (std::filesystem::filesystem_error&) {
      }
    }
    return ret;
  }

 private:
  const std::filesystem::path base_;

  mutable std::mutex mtx_;
  std::vector<std::string> imports_;


  void AddImport(const std::filesystem::path& p) noexcept {
    auto str = p.string();

    std::unique_lock<std::mutex> _ {mtx_};
    if (imports_.end() == std::find(imports_.begin(), imports_.end(), str)) {
      imports_.emplace_back(std::move(str));
    }
  }
};

}  // namespace nf7::luajit
