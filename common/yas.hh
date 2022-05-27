#pragma once

#include <string>
#include <iostream>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    std::unique_ptr<nf7::File>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::unique_ptr<nf7::File>& f) {
    ar(std::string(f->type().name()));
    f->Serialize(ar);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::unique_ptr<nf7::File>& f) {
    std::string name;
    ar(name);
    f = nf7::File::registry(name).Deserialize(nf7::Env::Peek(), ar);
    return ar;
  }
};

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::File::Path> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::File::Path& p) {
    p.Serialize(ar);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::File::Path& p) {
    p = {ar};
    return ar;
  }
};

}  // namespace detail
