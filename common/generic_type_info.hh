#pragma once

#include <exception>
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
class GenericTypeInfo : public nf7::File::TypeInfo {
 public:
  GenericTypeInfo(const std::string& name, std::unordered_set<std::string>&& v) noexcept :
      TypeInfo(name, AddFlags(std::move(v))) {
  }

  std::unique_ptr<nf7::File> Deserialize(nf7::Deserializer& ar) const override
  try {
    if constexpr (std::is_constructible<T, nf7::Deserializer&>::value) {
      return std::make_unique<T>(ar);
    } else {
      throw nf7::Exception {name() + " is not a deserializable"};
    }
  } catch (std::exception&) {
    throw nf7::DeserializeException {"deserialization failed ("+name()+")"};
  }
  std::unique_ptr<File> Create(nf7::Env& env) const override {
    if constexpr (std::is_constructible<T, nf7::Env&>::value) {
      return std::make_unique<T>(env);
    } else {
      throw nf7::Exception {name()+" has no default factory"};
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
    if (std::is_constructible<T, nf7::Env&>::value) {
      flags.insert("nf7::File::TypeInfo::Factory");
    }
    return flags;
  }
};

}  // namespace nf7
