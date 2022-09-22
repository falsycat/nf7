#pragma once

#include <algorithm>
#include <vector>


namespace nf7::util {

template <typename T>
inline void Uniq(std::vector<T>& v) noexcept {
  for (auto itr = v.begin(); itr < v.end();) {
    if (v.end() != std::find(itr+1, v.end(), *itr)) {
      itr = v.erase(itr);
    } else {
      ++itr;
    }
  }
}

}  // namespace nf7::util
