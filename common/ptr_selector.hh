#pragma once

#include <typeinfo>

#include "nf7.hh"


namespace nf7 {

template <typename Base, typename... I>
struct PtrSelector final {
 public:
  PtrSelector(const std::type_info& t) noexcept : type_(&t) { }
  PtrSelector(const PtrSelector&) = delete;
  PtrSelector(PtrSelector&&) = delete;
  PtrSelector& operator=(const PtrSelector&) = delete;
  PtrSelector& operator=(PtrSelector&&) = delete;

  template <typename T1, typename... T2>
  Base* Select(T1 ptr1, T2... ptr2) noexcept {
    auto ptr = Get<T1, I...>(ptr1);
    return ptr? ptr: Select(ptr2...);
  }
  Base* Select() noexcept { return nullptr; }

 private:
  template <typename T, typename I1, typename... I2>
  Base* Get(T ptr) const noexcept {
    if constexpr (std::is_base_of<I1, std::remove_pointer_t<T>>::value) {
      if (*type_ == typeid(I1)) return static_cast<I1*>(ptr);
    }
    return Get<T, I2...>(ptr);
  }
  template <typename T>
  Base* Get(T) const noexcept { return nullptr; }

  const std::type_info* const type_;
};

template <typename... I>
using InterfaceSelector = PtrSelector<File::Interface, I...>;

}  // namespace nf7
