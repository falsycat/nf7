#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

#include "nf7.hh"


namespace nf7 {

class FileBase : public nf7::File {
 public:
  class Feature {
   public:
    Feature() = delete;
    Feature(nf7::FileBase& f) noexcept {
      f.feats_.push_back(this);
    }
    virtual ~Feature() = default;
    Feature(const Feature&) = delete;
    Feature(Feature&&) = delete;
    Feature& operator=(const Feature&) = delete;
    Feature& operator=(Feature&&) = delete;

    virtual nf7::File* Find(std::string_view) const noexcept { return nullptr; }
    virtual void Handle(const nf7::File::Event&) noexcept { }
    virtual void Update() noexcept { }
  };

  using nf7::File::File;

  nf7::File* Find(std::string_view name) const noexcept override {
    for (auto feat : feats_) {
      if (auto ret = feat->Find(name)) {
        return ret;
      }
    }
    return nullptr;
  }
  void Handle(const nf7::File::Event& ev) noexcept override {
    for (auto feat : feats_) {
      feat->Handle(ev);
    }
  }
  void Update() noexcept override {
    for (auto feat : feats_) {
      feat->Update();
    }
  }

 private:
  std::vector<Feature*> feats_;
};

}  // namespace nf7
