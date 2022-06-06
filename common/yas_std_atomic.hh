#pragma once

#include <atomic>

#include <yas/serialize.hpp>


namespace yas::detail {

template <size_t F, typename T>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    std::atomic<T>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::atomic<T>& v) {
    ar(v.load());
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::atomic<T>& v) {
    T temp;
    ar(temp);
    v.store(temp);
    return ar;
  }
};

}  // namespace yas::detail
