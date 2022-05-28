#pragma once

#include "nf7.hh"


namespace nf7 {

template <typename T>
class GenericTypeInfo : public File::TypeInfo {
 public:
  GenericTypeInfo(const std::string& name, std::unordered_set<std::string>&& v) noexcept :
      TypeInfo(name, AddFlags(std::move(v))) {
  }

  std::unique_ptr<File> Deserialize(Env& env, Deserializer& d) const override
  try {
    return std::make_unique<T>(env, d);
  } catch (Exception&) {
    throw DeserializeException(std::string(name())+" deserialization failed");
  }
  std::unique_ptr<File> Create(Env& env) const noexcept override {
    if constexpr (std::is_constructible<T, Env&>::value) {
      return std::make_unique<T>(env);
    } else {
      return nullptr;
    }
  }

 private:
  static std::unordered_set<std::string> AddFlags(
      std::unordered_set<std::string>&& flags) noexcept {
    if (std::is_constructible<T, Env&>::value) flags.insert("File_Factory");
    return flags;
  }
};

}  // namespace nf7
