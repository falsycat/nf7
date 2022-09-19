#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>


namespace nf7::util {

inline std::string_view Trim(
    std::string_view str,
    const std::function<bool(char)>& func = [](auto c) { return std::isspace(c); }) noexcept {
  while (!str.empty() && func(str.front())) {
    str.remove_prefix(1);
  }
  while (!str.empty() && func(str.back())) {
    str.remove_suffix(1);
  }
  return str;
}

inline std::optional<std::string_view> IterateTerms(std::string_view str, char c, size_t& i) noexcept {
  std::string_view ret;
  while (ret.empty() && i < str.size()) {
    auto j = str.find(c, i);
    if (j == std::string::npos) j = str.size();

    ret = str.substr(i, j-i);
    i = j+1;
  }
  if (ret.empty()) return std::nullopt;
  return ret;
}

inline void SplitAndAppend(std::vector<std::string>& dst, std::string_view src, char c = '\n') noexcept {
  size_t itr = 0;
  while (auto term = IterateTerms(src, c, itr)) {
    dst.emplace_back(*term);
  }
}
inline void JoinAndAppend(std::string& dst, std::span<const std::string> src, char c = '\n') noexcept {
  for (auto& str : src) {
    dst += str;
    dst += c;
  }
}

inline std::optional<std::string_view> SplitAndValidate(
    std::string_view v,
    const std::function<bool(std::string_view)> validator, char c = '\n') noexcept {
  size_t itr = 0;
  while (auto term = IterateTerms(v, c, itr)) {
    if (validator(*term)) {
      return term;
    }
  }
  return std::nullopt;
}
inline std::optional<std::string_view> SplitAndValidate(
    std::string_view v,
    const std::function<void(std::string_view)> validator, char c = '\n') noexcept {
  size_t itr = 0;
  while (auto term = IterateTerms(v, c, itr)) {
    try {
      validator(*term);
    } catch (nf7::Exception&) {
      return term;
    }
  }
  return std::nullopt;
}

}  // namespace nf7::util
