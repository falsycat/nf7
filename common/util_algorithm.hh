#pragma once

#include <algorithm>
#include <vector>


namespace nf7::util {

template <typename T>
inline size_t Uniq(std::vector<T>& v) noexcept {
  size_t n = 0;
  for (auto itr = v.begin(); itr < v.end();) {
    if (v.end() != std::find(itr+1, v.end(), *itr)) {
      itr = v.erase(itr);
      ++n;
    } else {
      ++itr;
    }
  }
  return n;
}

}  // namespace nf7::util
