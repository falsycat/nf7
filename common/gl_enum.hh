#pragma once

#include <cassert>
#include <cstdint>
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


enum class BufferTarget {
  Array,
  Element,
};
template <>
struct EnumMeta<BufferTarget> {
  static inline const std::unordered_map<BufferTarget, GLenum> glmap = {
    {BufferTarget::Array,   GL_ARRAY_BUFFER},
    {BufferTarget::Element, GL_ELEMENT_ARRAY_BUFFER},
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
inline GLenum ToInternalFormat(NumericType n, ColorComp c) {
  GLenum ret = 0;
  switch (n) {
  case NumericType::U8:
    ret = 
        c == ColorComp::R?    GL_R8:
        c == ColorComp::RG?   GL_RG8:
        c == ColorComp::RGB?  GL_RGB8:
        c == ColorComp::RGBA? GL_RGBA8: 0;
    break;
  case NumericType::F32:
    ret =
        c == ColorComp::R?    GL_R32F:
        c == ColorComp::RG?   GL_RG32F:
        c == ColorComp::RGB?  GL_RGB32F:
        c == ColorComp::RGBA? GL_RGBA32F: 0;
    break;
  default:
    break;
  }
  if (ret == 0) {
    throw nf7::Exception {"invalid numtype and comp pair"};
  }
  return ret;
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
