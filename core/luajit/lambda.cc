// No copyright
#include "core/luajit/lambda.hh"

#include <chrono>
#include <cstdint>

#include "core/luajit/context.hh"

using namespace std::literals;


namespace nf7::core::luajit {

class Lambda::Thread : public luajit::Thread {
 public:
  using luajit::Thread::Thread;

  void Attach(const std::shared_ptr<Lambda>& la) noexcept {
    recvq_size_before_run_ = la->recvq_.size();
    recv_count_before_run_ = la->recv_count_;
    la_ = la;
  }

 private:
  void onExited(TaskContext& lua) noexcept override {
    if (auto la = la_.lock()) {
      ++la->exit_count_;
      TryResume(lua, la);
    }
  }
  void onAborted(TaskContext& lua) noexcept override {
    if (auto la = la_.lock()) {
      if (auto logger = la->logger_) {
        const auto msg = lua_tostring(*lua, -1);
        logger->Error(msg);
      }
      ++la->abort_count_;
      TryResume(lua, la);
    }
  }
  void TryResume(TaskContext& lua, const std::shared_ptr<Lambda>& la) noexcept {
    auto self = std::move(la->thread_);

    const bool no_pop  = recvq_size_before_run_ == la->recvq_.size();
    const bool no_push = recv_count_before_run_ == la->recv_count_;
    if ((no_pop && no_push) || la->recvq_.empty()) {
      return;
    }
    lua.context()->Exec([wla = la_](auto& lua) {
      if (auto la = wla.lock()) {
        la->Resume(lua);
      }
    });
  }

 private:
  std::weak_ptr<Lambda> la_;
  size_t recvq_size_before_run_ = 0;
  size_t recv_count_before_run_ = 0;
};


Lambda::Lambda(nf7::Env& env,
               const std::shared_ptr<Value>& func,
               const std::shared_ptr<subsys::Maker<IO>>& maker)
    : nf7::Lambda(), Observer<IO>(*maker),
      clock_(env.GetOr<subsys::Clock>()),
      concurrency_(env.Get<subsys::Concurrency>()),
      logger_(env.GetOr<subsys::Logger>(NullLogger::kInstance)),
      maker_(maker),
      taker_(env.GetOr<subsys::Taker<IO>>(NullTaker<IO>::kInstance)),
      lua_(env.Get<luajit::Context>()),
      func_(func) { }

void Lambda::Notify(const IO& v) noexcept {
  lua_->Exec([this, self = shared_from_this(), v](auto& lua) {
    recvq_.push_back(v);
    ++recv_count_;
    Resume(lua);
  });
}

void Lambda::Resume(TaskContext& lua) noexcept {
  if (recvq_.empty()) {
    // skip resuming until this lambda takes next value if the queue is empty
    return;
  }

  if (!ctx_) {
    // create context if it's a first time
    PushLuaContextObject(lua);
    ctx_ = lua.Register();
  }
  if (awaiting_value_ && nullptr != thread_) {
    // thread is paused by recv() so resume it with a value
    const auto v = recvq_.front();
    recvq_.pop_front();
    thread_->Resume(lua, v);
  } else {
    if (nullptr != thread_) {
      // the active thread is paused by a reason except recv()
      // in this case, thread_->Resume() is done by one who yielded
      return;
    }
    // start the thread
    thread_ = luajit::Thread::Make<Lambda::Thread>(lua, func_);
    thread_->Attach(shared_from_this());
    thread_->Resume(lua, ctx_);
  }
}

void Lambda::PushLuaContextObject(TaskContext& lua) noexcept {
  static const auto kName = "nf7::core::luajit::Lambda";
  static const auto self  = [](auto L) {
    auto la = TaskContext::
        CheckUserData<std::weak_ptr<Lambda>>(L, 1, kName).lock();
    if (!la) {
      luaL_error(L, "lambda expired");
    }
    return la;
  };

  lua.NewUserData(weak_from_this());
  if (luaL_newmetatable(*lua, kName)) {
    lua_pushcfunction(*lua, [](auto L) {
      TaskContext::
          CheckUserData<std::weak_ptr<Lambda>>(L, 1, kName).~weak_ptr();
      return 0;
    });
    lua_setfield(*lua, -2, "__gc");

    lua_createtable(*lua, 0, 0);
    {
      lua_pushcfunction(*lua, [](auto L) {
        const auto la = self(L);
        if (la->recvq_.empty()) {
          la->awaiting_value_ = true;
          return lua_yield(L, 0);
        }
        (TaskContext {la->lua_, L}).Push(la->recvq_.front());
        la->recvq_.pop_front();
        return 1;
      });
      lua_setfield(*lua, -2, "recv");

      lua_pushcfunction(*lua, [](auto L) {
        const auto la = self(L);
        const auto v  = (TaskContext {la->lua_, L}).CheckValue(2);
        la->concurrency_->Exec([la, v = v](auto&) mutable {
          la->taker_->Take(std::move(v));
        });
        return 1;
      });
      lua_setfield(*lua, -2, "send");

      lua_pushcfunction(*lua, [](auto L) {
        const auto la = self(L);
        if (nullptr == la->clock_) {
          return luaL_error(L, "clock is not installed in the environment");
        }

        const auto wla   = std::weak_ptr<Lambda> {la};
        const auto dur   = luaL_checkinteger(L, 2);
        const auto after = la->clock_->now() + dur * 1ms;

        la->lua_->Push(Task {after, [wla](auto& lua) {
          if (auto la = wla.lock()) {
            assert(la->thread_);
            la->thread_->Resume(lua);
          }
        }});
        return lua_yield(L, 0);
      });
      lua_setfield(*lua, -2, "sleep");

      static const auto logFunc = []<subsys::Logger::Level lv>(auto L) {
        const auto la       = self(L);
        const auto contents = luaL_checkstring(L, 2);
        la->logger_->Push(subsys::Logger::Item {lv, contents});
        return 0;
      };
      lua_pushcfunction(*lua, [](auto L) {
        return logFunc.operator()<subsys::Logger::kTrace>(L);
      });
      lua_setfield(*lua, -2, "trace");

      lua_pushcfunction(*lua, [](auto L) {
        return logFunc.operator()<subsys::Logger::kInfo>(L);
      });
      lua_setfield(*lua, -2, "info");

      lua_pushcfunction(*lua, [](auto L) {
        return logFunc.operator()<subsys::Logger::kWarn>(L);
      });
      lua_setfield(*lua, -2, "warn");

      lua_pushcfunction(*lua, [](auto L) {
        return logFunc.operator()<subsys::Logger::kError>(L);
      });
      lua_setfield(*lua, -2, "error");
    }
    lua_setfield(*lua, -2, "__index");
  }
  lua_setmetatable(*lua, -2);
}

}  // namespace nf7::core::luajit
