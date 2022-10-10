#pragma once

#include <string_view>

#include <magic_enum.hpp>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"


namespace nf7 {

template <typename T>
struct EnumSerializer {
 public:
  static auto& save(auto& ar, auto t) {
    ar(magic_enum::enum_name(t));
    return ar;
  }
  static auto& load(auto& ar, auto& t) {
    std::string v;
    ar(v);
    if (auto ot = magic_enum::enum_cast<T>(v)) {
      t = *ot;
    } else {
      throw nf7::DeserializeException {"unknown enum: "+v};
    }
    return ar;
  }
};

#define NF7_YAS_DEFINE_ENUM_SERIALIZER(T)  \
    template <size_t F>  \
    struct serializer<  \
        yas::detail::type_prop::is_enum,  \
        yas::detail::ser_case::use_internal_serializer,  \
        F, T> : nf7::EnumSerializer<T> {  \
    }

}  // namespace nf7
