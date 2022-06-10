#pragma once

#include <unordered_map>

#include "nf7.hh"


namespace nf7 {

class GenericWatcher final : public Env::Watcher {
 public:
  using Handler = std::function<void(const File::Event&)>;

  GenericWatcher(Env& env) noexcept : Watcher(env) {
  }

  void AddHandler(File::Event::Type type, Handler&& h) noexcept {
    handlers_[type] = std::move(h);
  }
  void Handle(const File::Event& ev) noexcept override {
    auto handler = handlers_.find(ev.type);
    if (handler == handlers_.end()) return;
    handler->second(ev);
  }

 private:
  std::unordered_map<File::Event::Type, Handler> handlers_;
};

}  // namespace nf7
