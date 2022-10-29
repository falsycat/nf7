#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>

#include <GL/glew.h>

#include <magic_enum.hpp>

#include "nf7.hh"

#include "common/yas_enum.hh"


namespace nf7::gl {

template <typename T>
struct EnumMeta {};

template <typename T>
GLenum ToEnum(T v) noexcept {
  assert(EnumMeta<T>::glmap.contains(v));
  return EnumMeta<T>::glmap.find(v)->second;
}
template <typename T>
GLenum ToEnum(const std::string& v)
try {
  return ToEnum(magic_enum::enum_cast<T>(v).value());
} catch (std::bad_optional_access&) {
  throw nf7::Exception {"unknown enum: "+v};
}

enum class NumericType : uint8_t {
  U8  = 0x01,
  I8  = 0x11,
  U16 = 0x02,
  I16 = 0x12,
  U32 = 0x04,
  I32 = 0x14,
  F16 = 0x22,
  F32 = 0x24,
  F64 = 0x28,
};
template <>
struct EnumMeta<NumericType> {
  static inline const std::unordered_map<NumericType, GLenum> glmap = {
    {NumericType::U8, GL_UNSIGNED_BYTE},
    {NumericType::I8, GL_BYTE},
    {NumericType::U16, GL_UNSIGNED_SHORT},
    {NumericType::I16, GL_SHORT},
    {NumericType::U32, GL_UNSIGNED_INT},
    {NumericType::I32, GL_INT},
    {NumericType::F16, GL_HALF_FLOAT},
    {NumericType::F32, GL_FLOAT},
    {NumericType::F64, GL_DOUBLE},
  };
};
inline uint8_t GetByteSize(NumericType t) noexcept {
  return magic_enum::enum_integer(t) & 0xF;
}


enum class ColorComp : uint8_t {
  R    = 0x01,
  G    = 0x11,
  B    = 0x21,
  RG   = 0x02,
  RGB  = 0x03,
  RGBA = 0x04,
};
template <>
struct EnumMeta<ColorComp> {
  static inline const std::unordered_map<ColorComp, GLenum> glmap = {
    {ColorComp::R,    GL_RED},
    {ColorComp::G,    GL_GREEN},
    {ColorComp::B,    GL_BLUE},
    {ColorComp::RG,   GL_RG},
    {ColorComp::RGB,  GL_RGB},
    {ColorComp::RGBA, GL_RGBA},
  };
};
inline uint8_t GetCompCount(ColorComp c) noexcept {
  return magic_enum::enum_integer(c) & 0xF;
}


enum class InternalFormat : uint8_t {
  R8    = 0x01,
  RG8   = 0x02,
  RGB8  = 0x03,
  RGBA8 = 0x04,

  RF32    = 0x11,
  RGF32   = 0x12,
  RGBF32  = 0x13,
  RGBAF32 = 0x14,

  Depth16  = 0x21,
  Depth24  = 0x31,
  DepthF32 = 0x41,

  Depth24_Stencil8  = 0x22,
  DepthF32_Stencil8 = 0x32,
};
template <>
struct EnumMeta<InternalFormat> {
  static inline const std::unordered_map<InternalFormat, GLenum> glmap = {
    {InternalFormat::R8,                 GL_R8},
    {InternalFormat::RG8,                GL_RG8},
    {InternalFormat::RGB8,               GL_RGB8},
    {InternalFormat::RGBA8,              GL_RGBA8},
    {InternalFormat::RF32,               GL_R32F},
    {InternalFormat::RGF32,              GL_RG32F},
    {InternalFormat::RGBF32,             GL_RGB32F},
    {InternalFormat::RGBAF32,            GL_RGBA32F},
    {InternalFormat::Depth16,            GL_DEPTH_COMPONENT16},
    {InternalFormat::Depth24,            GL_DEPTH_COMPONENT24},
    {InternalFormat::DepthF32,           GL_DEPTH_COMPONENT32F},
    {InternalFormat::Depth24_Stencil8,  GL_DEPTH24_STENCIL8},
    {InternalFormat::DepthF32_Stencil8, GL_DEPTH32F_STENCIL8},
  };
};
inline uint8_t GetByteSize(InternalFormat fmt) noexcept {
  switch (fmt) {
  case InternalFormat::R8:                return 1;
  case InternalFormat::RG8:               return 2;
  case InternalFormat::RGB8:              return 3;
  case InternalFormat::RGBA8:             return 4;
  case InternalFormat::RF32:              return 4;
  case InternalFormat::RGF32:             return 8;
  case InternalFormat::RGBF32:            return 12;
  case InternalFormat::RGBAF32:           return 16;
  case InternalFormat::Depth16:           return 2;
  case InternalFormat::Depth24:           return 3;
  case InternalFormat::DepthF32:          return 4;
  case InternalFormat::Depth24_Stencil8:  return 4;
  case InternalFormat::DepthF32_Stencil8: return 5;
  }
  std::abort();
}
inline ColorComp GetColorComp(InternalFormat fmt) {
  switch (fmt) {
  case InternalFormat::R8:      return ColorComp::R;
  case InternalFormat::RG8:     return ColorComp::RG;
  case InternalFormat::RGB8:    return ColorComp::RGB;
  case InternalFormat::RGBA8:   return ColorComp::RGBA;
  case InternalFormat::RF32:    return ColorComp::R;
  case InternalFormat::RGF32:   return ColorComp::RG;
  case InternalFormat::RGBF32:  return ColorComp::RGB;
  case InternalFormat::RGBAF32: return ColorComp::RGBA;
  default: throw nf7::Exception {"does not have color component"};
  }
}
inline NumericType GetNumericType(InternalFormat fmt) {
  switch (fmt) {
  case InternalFormat::R8:
  case InternalFormat::RG8:
  case InternalFormat::RGB8:
  case InternalFormat::RGBA8:
    return NumericType::U8;
  case InternalFormat::RF32:
  case InternalFormat::RGF32:
  case InternalFormat::RGBF32:
  case InternalFormat::RGBAF32:
    return NumericType::F32;
  default:
    throw nf7::Exception {"does not have color component"};
  }
}
inline bool IsColor(InternalFormat fmt) noexcept {
  return (magic_enum::enum_integer(fmt) & 0xF0) <= 1;
}
inline bool HasDepth(InternalFormat fmt) noexcept {
  return !IsColor(fmt);
}
inline bool HasStencil(InternalFormat fmt) noexcept {
  return
      fmt == InternalFormat::Depth24_Stencil8 ||
      fmt == InternalFormat::DepthF32_Stencil8;
}


enum class BufferTarget {
  Array,
  ElementArray,
};
template <>
struct EnumMeta<BufferTarget> {
  static inline const std::unordered_map<BufferTarget, GLenum> glmap = {
    {BufferTarget::Array,        GL_ARRAY_BUFFER},
    {BufferTarget::ElementArray, GL_ELEMENT_ARRAY_BUFFER},
  };
};

enum class BufferUsage {
  StaticDraw,
  DynamicDraw,
  StreamDraw,
  StaticRead,
  DynamicRead,
  StreamRead,
  StaticCopy,
  DynamicCopy,
  StreamCopy,
};
template <>
struct EnumMeta<BufferUsage> {
  static inline const std::unordered_map<BufferUsage, GLenum> glmap = {
    {BufferUsage::StaticDraw,  GL_STATIC_DRAW},
    {BufferUsage::DynamicDraw, GL_DYNAMIC_DRAW},
    {BufferUsage::DynamicDraw, GL_STREAM_DRAW},
    {BufferUsage::StaticRead,  GL_STATIC_READ},
    {BufferUsage::DynamicRead, GL_DYNAMIC_READ},
    {BufferUsage::DynamicRead, GL_STREAM_READ},
    {BufferUsage::StaticCopy,  GL_STATIC_COPY},
    {BufferUsage::DynamicCopy, GL_DYNAMIC_COPY},
    {BufferUsage::DynamicCopy, GL_STREAM_COPY},
  };
};


enum class TextureTarget : uint8_t {
  Tex2D = 0x02,
  Rect  = 0x12,
};
template <>
struct EnumMeta<TextureTarget> {
  static inline const std::unordered_map<TextureTarget, GLenum> glmap = {
    {TextureTarget::Tex2D, GL_TEXTURE_2D},
    {TextureTarget::Rect,  GL_TEXTURE_RECTANGLE},
  };
};
inline uint8_t GetDimension(TextureTarget t) noexcept {
  return magic_enum::enum_integer(t) & 0xF;
}


enum class ShaderType {
  Vertex,
  Fragment,
};
template <>
struct EnumMeta<ShaderType> {
  static inline const std::unordered_map<ShaderType, GLenum> glmap = {
    {ShaderType::Vertex,   GL_VERTEX_SHADER},
    {ShaderType::Fragment, GL_FRAGMENT_SHADER},
  };
};


enum class DrawMode {
  Points,
  LineStrip,
  LineLoop,
  Lines,
  LineStripAdjacency,
  LinesAdjacency,
  TriangleStrip,
  TriangleFan,
  Triangles,
  TriangleStripAdjacency,
  TrianglesAdjacency,
};
template <>
struct EnumMeta<DrawMode> {
  static inline const std::unordered_map<DrawMode, GLenum> glmap = {
    {DrawMode::Points,                 GL_POINTS},
    {DrawMode::LineStrip,              GL_LINE_STRIP},
    {DrawMode::LineLoop,               GL_LINE_LOOP},
    {DrawMode::Lines,                  GL_LINES},
    {DrawMode::LineStripAdjacency,     GL_LINE_STRIP_ADJACENCY},
    {DrawMode::LinesAdjacency,         GL_LINES_ADJACENCY},
    {DrawMode::TriangleStrip,          GL_TRIANGLE_STRIP},
    {DrawMode::TriangleFan,            GL_TRIANGLE_FAN},
    {DrawMode::Triangles,              GL_TRIANGLES},
    {DrawMode::TriangleStripAdjacency, GL_TRIANGLE_STRIP_ADJACENCY},
    {DrawMode::TrianglesAdjacency,     GL_TRIANGLES_ADJACENCY},
  };
};


enum class FramebufferSlot {
  Color0,
  Color1,
  Color2,
  Color3,
  Color4,
  Color5,
  Color6,
  Color7,
};
template <>
struct EnumMeta<FramebufferSlot> {
  static inline const std::unordered_map<FramebufferSlot, GLenum> glmap = {
    {FramebufferSlot::Color0, GL_COLOR_ATTACHMENT0},
    {FramebufferSlot::Color1, GL_COLOR_ATTACHMENT0+1},
    {FramebufferSlot::Color2, GL_COLOR_ATTACHMENT0+2},
    {FramebufferSlot::Color3, GL_COLOR_ATTACHMENT0+3},
    {FramebufferSlot::Color4, GL_COLOR_ATTACHMENT0+4},
    {FramebufferSlot::Color5, GL_COLOR_ATTACHMENT0+5},
    {FramebufferSlot::Color6, GL_COLOR_ATTACHMENT0+6},
    {FramebufferSlot::Color7, GL_COLOR_ATTACHMENT0+7},
  };
};

}  // namespace nf7::gl


namespace yas::detail {

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::NumericType);
NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::ColorComp);

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::BufferTarget);
NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::BufferUsage);

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::TextureTarget);

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::ShaderType);

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::DrawMode);

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::gl::FramebufferSlot);

}  // namespace yas::detail
