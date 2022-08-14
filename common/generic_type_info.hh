#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <imgui.h>

#include "nf7.hh"


namespace nf7 {

template <typename T>
concept GenericTypeInfo_UpdateTooltip_ = requires() { T::UpdateTypeTooltip(); };

template <typename T>
concept GenericTypeInfo_Description_ = requires() { T::kTypeDescription; };


template <typename T>
class GenericTypeInfo : public File::TypeInfo {
 public:
  GenericTypeInfo(const std::string& name, std::unordered_set<std::string>&& v) noexcept :
      TypeInfo(name, AddFlags(std::move(v))) {
  }

  std::unique_ptr<File> Deserialize(Env& env, Deserializer& d) const override
  try {
    return std::make_unique<T>(env, d);
  } catch (nf7::Exception&) {
    throw nf7::DeserializeException {"deserialization failed"};
  }
  std::unique_ptr<File> Create(Env& env) const override {
    if constexpr (std::is_constructible<T, Env&>::value) {
      return std::make_unique<T>(env);
    } else {
      throw nf7::Exception {name()+" has no factory without parameters"};
    }
  }

  void UpdateTooltip() const noexcept override {
    if constexpr (nf7::GenericTypeInfo_UpdateTooltip_<T>) {
      T::UpdateTypeTooltip();
    } else if constexpr (nf7::GenericTypeInfo_Description_<T>) {
      ImGui::TextUnformatted(T::kTypeDescription);
    } else {
      ImGui::TextUnformatted("(no description)");
    }
  }

 private:
  static std::unordered_set<std::string> AddFlags(
      std::unordered_set<std::string>&& flags) noexcept {
    if (std::is_constructible<T, Env&>::value) {
      flags.insert("nf7::File::TypeInfo::Factory");
    }
    return flags;
  }
};

}  // namespace nf7
