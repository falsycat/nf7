#pragma once

#include <imgui.h>
#include <linalg.h>


namespace linalg {

using namespace aliases;

template <>
struct converter<float2, ImVec2> {
  float2 operator() (const ImVec2& v) const { return {v.x, v.y}; }
};
template <>
struct converter<ImVec2, float2> {
  ImVec2 operator() (const float2& v) const { return {v.x, v.y}; }
};

}  // namespace linalg
