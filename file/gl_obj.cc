#include <cinttypes>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include <magic_enum.hpp>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/factory.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/generic_watcher.hh"
#include "common/gl_fence.hh"
#include "common/gl_obj.hh"
#include "common/gui_config.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_enum.hh"


namespace nf7 {
namespace {

template <typename T>
class ObjBase : public nf7::FileBase,
    public nf7::DirItem, public nf7::Node,
    public nf7::AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<typename T::Product>>> {
 public:
  using ThisObjBase     = ObjBase<T>;
  using Product         = std::shared_ptr<typename T::Product>;
  using Resource        = nf7::Mutex::Resource<Product>;
  using ResourceFuture  = nf7::Future<Resource>;
  using ResourcePromise = typename ResourceFuture::Promise;

  struct TypeInfo;

  static void UpdateTypeTooltip() noexcept {
    T::UpdateTypeTooltip();
  }

  ObjBase(nf7::Env& env, T&& data = {}) noexcept :
      nf7::FileBase(TypeInfo::kType, env, {&log_}),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kNone), life_(*this), log_(*this),
      mem_(std::move(data), *this) {
    mem_.onRestore = mem_.onCommit = [this]() {
      Drop();
    };
  }

  ObjBase(nf7::Deserializer& ar) : ObjBase(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<ThisObjBase>(env, T {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override {
    return std::make_shared<Lambda>(*this, parent);
  }
  std::span<const std::string> GetInputs() const noexcept override {
    return T::kInputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return T::kOutputs;
  }

  ResourceFuture Create() noexcept final {
    return Create(false);
  }
  ResourceFuture Create(bool ex) noexcept {
    auto ctx = std::make_shared<nf7::GenericContext>(*this, "OpenGL obj factory");

    ResourcePromise pro {ctx};
    mtx_.AcquireLock(ctx, ex).ThenIf([this, ctx, pro](auto& k) mutable {
      if (!fu_) {
        watcher_.emplace(env());
        fu_ = mem_->Create(ctx, *watcher_);
        watcher_->AddHandler(nf7::File::Event::kUpdate, [this](auto&) { Drop(); });
      }
      fu_->Chain(pro, [k](auto& obj) { return Resource {k, obj}; });
    });
    return pro.future().
        template Catch<nf7::Exception>(ctx, [this](auto& e) {
          log_.Error(e);
        });
  }

  void UpdateMenu() noexcept override {
    if (ImGui::BeginMenu("object management")) {
      if (ImGui::MenuItem("create", nullptr, false, !fu_)) {
        Create();
      }
      if (ImGui::MenuItem("drop", nullptr, false, !!fu_)) {
        Drop();
      }
      if (ImGui::MenuItem("drop and create")) {
        Drop();
        Create();
      }
      ImGui::EndMenu();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("these actions can cause CORRUPTION of running lambdas");
    }
    if constexpr (nf7::gui::ConfigData<T>) {
      if (ImGui::BeginMenu("config")) {
        nf7::gui::Config(mem_);
        ImGui::EndMenu();
      }
    }
  }
  void UpdateTooltip() noexcept override {
    const char* status = "(unknown)";
    if (fu_) {
      if (fu_->done()) {
        status = "ready";
      } else if (fu_->error()) {
        status = "error";
      } else {
        status = "creating";
      }
    } else {
      status = "unused";
    }
    ImGui::Text("status: %s", status);
    ImGui::Spacing();

    const auto prod = fu_ && fu_->done()? fu_->value(): nullptr;
    mem_->UpdateTooltip(prod);
  }

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node,
        nf7::AsyncFactory<std::shared_ptr<T>>>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<ThisObjBase>             life_;
  nf7::LoggerRef                     log_;
  std::optional<nf7::GenericWatcher> watcher_;

  nf7::Mutex mtx_;

  std::optional<nf7::Future<Product>> fu_;

  nf7::GenericMemento<T> mem_;


  void Drop() noexcept {
    auto ctx = std::make_shared<nf7::GenericContext>(*this, "dropping OpenGL obj");
    mtx_.AcquireLock(ctx, true  /* = exclusive */).
        ThenIf([this](auto&) {
          fu_ = std::nullopt;
          Touch();
        });
  }


  class Lambda final : public nf7::Node::Lambda,
      public std::enable_shared_from_this<ThisObjBase::Lambda> {
   public:
    Lambda(ThisObjBase& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
        nf7::Node::Lambda(f, parent), f_(f.life_) {
    }

    void Handle(const nf7::Node::Lambda::Msg& in) noexcept final {
      if (!f_) return;

      f_->Create(true  /* = exclusive */).
          ThenIf(shared_from_this(), [this, in](auto& obj) {
            try {
              if (f_ && f_->mem_->Handle(shared_from_this(), obj, in)) {
                f_->Touch();
              }
            } catch (nf7::Exception& e) {
              f_->log_.Error(e);
            }
          });
    }

   private:
    using std::enable_shared_from_this<Lambda>::shared_from_this;

    nf7::Life<ThisObjBase>::Ref f_;
  };
};


struct Buffer {
 public:
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("OpenGL buffer");
  }

  static inline const std::vector<std::string> kInputs = {
    "upload",
  };
  static inline const std::vector<std::string> kOutputs = {
  };

  using Product = nf7::gl::Buffer;

  enum class Type {
    Array,
    Element,
  };
  enum class Usage {
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

  Buffer() = default;
  Buffer(const Buffer&) = default;
  Buffer(Buffer&&) = default;
  Buffer& operator=(const Buffer&) = default;
  Buffer& operator=(Buffer&&) = default;

  void serialize(auto& ar) {
    ar(type_, usage_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "type";
    st << YAML::Value << std::string {magic_enum::enum_name(type_)};
    st << YAML::Key   << "usage";
    st << YAML::Value << std::string {magic_enum::enum_name(usage_)};
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }

  void Parse(const std::string& v)
  try {
    const auto yaml = YAML::Load(v);

    const auto new_type = magic_enum::
        enum_cast<Type>(yaml["type"].as<std::string>()).value();
    const auto new_usage = magic_enum::
        enum_cast<Usage>(yaml["usage"].as<std::string>()).value();

    type_  = new_type;
    usage_ = new_usage;
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {"unknown enum"};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(
      const std::shared_ptr<nf7::Context>& ctx, nf7::Env::Watcher&) noexcept {
    nf7::Future<std::shared_ptr<Product>>::Promise pro {ctx};
    ctx->env().ExecGL(ctx, [ctx, pro, t = type_]() mutable {
      pro.Return(std::make_shared<Product>(ctx, GLuint {0}, FromType(t)));
    });
    return pro.future();
  }

  bool Handle(const std::shared_ptr<nf7::Node::Lambda>&             handler,
              const nf7::Mutex::Resource<std::shared_ptr<Product>>& res,
              const nf7::Node::Lambda::Msg&                         in) {
    if (in.name == "upload") {
      const auto& vec = in.value.vector();
      if (vec->size() == 0) return false;

      const auto usage = FromUsage(usage_);
      handler->env().ExecGL(handler, [res, vec, usage]() {
        const auto n = static_cast<GLsizeiptr>(vec->size());

        auto& buf = **res;
        auto& m   = buf.meta();
        glBindBuffer(m.type, buf.id());
        {
          if (m.size != vec->size()) {
            m.size = vec->size();
            glBufferData(m.type, n, vec->data(), usage);
          } else {
            glBufferSubData(m.type, 0, n, vec->data());
          }
        }
        glBindBuffer(m.type, 0);
        assert(0 == glGetError());
      });
      return true;
    } else {
      throw nf7::Exception {"unknown input: "+in.name};
    }
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    const auto t = magic_enum::enum_name(type_);
    ImGui::Text("type: %.*s", static_cast<int>(t.size()), t.data());
    if (prod) {
      ImGui::Text("  id: %zu", static_cast<size_t>(prod->id()));
      ImGui::Text("size: %zu bytes", prod->meta().size);
    }
  }

 private:
  Type  type_  = Type::Array;
  Usage usage_ = Usage::StaticDraw;

  static GLenum FromType(Type t) {
    return
        t == Type::Array?   GL_ARRAY_BUFFER:
        t == Type::Element? GL_ELEMENT_ARRAY_BUFFER:
        throw 0;
  }
  static GLenum FromUsage(Usage u) {
    return
        u == Usage::StaticDraw?  GL_STATIC_DRAW:
        u == Usage::DynamicDraw? GL_DYNAMIC_DRAW:
        u == Usage::StreamDraw?  GL_STREAM_DRAW:
        u == Usage::StaticRead?  GL_STATIC_READ:
        u == Usage::DynamicRead? GL_DYNAMIC_READ:
        u == Usage::StreamRead?  GL_STREAM_READ:
        u == Usage::StaticCopy?  GL_STATIC_COPY:
        u == Usage::DynamicCopy? GL_DYNAMIC_COPY:
        u == Usage::StreamCopy?  GL_STREAM_COPY:
        throw 0;
  }
};
template <>
struct ObjBase<Buffer>::TypeInfo final {
  static inline const nf7::GenericTypeInfo<ObjBase<Buffer>> kType = {"GL/Buffer", {"nf7::DirItem"}};
};


struct Texture {
 public:
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("OpenGL texture");
  }

  static inline const std::vector<std::string> kInputs = {
    "upload",
  };
  static inline const std::vector<std::string> kOutputs = {
  };

  using Product = nf7::gl::Texture;

  enum class Type {
    Tex2D = 0x20,
    Rect  = 0x21,
  };
  enum class Format {
    U8   = 0x01,
    F32  = 0x14,
  };
  enum class Comp : uint8_t {
    R    = 1,
    RG   = 2,
    RGB  = 3,
    RGBA = 4,
  };

  Texture() = default;
  Texture(const Texture&) = default;
  Texture(Texture&&) = default;
  Texture& operator=(const Texture&) = default;
  Texture& operator=(Texture&&) = default;

  void serialize(auto& ar) {
    ar(type_, format_, comp_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "type";
    st << YAML::Value << std::string {magic_enum::enum_name(type_)};
    st << YAML::Key   << "format";
    st << YAML::Value << std::string {magic_enum::enum_name(format_)};
    st << YAML::Key   << "comp";
    st << YAML::Value << std::string {magic_enum::enum_name(comp_)};
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }

  void Parse(const std::string& v)
  try {
    const auto yaml = YAML::Load(v);

    const auto new_type = magic_enum::
        enum_cast<Type>(yaml["type"].as<std::string>()).value();
    const auto new_format = magic_enum::
        enum_cast<Format>(yaml["format"].as<std::string>()).value();
    const auto new_comp = magic_enum::
        enum_cast<Comp>(yaml["comp"].as<std::string>()).value();

    type_   = new_type;
    format_ = new_format;
    comp_   = new_comp;
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {"unknown enum"};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(
      const std::shared_ptr<nf7::Context>& ctx, nf7::Env::Watcher&) noexcept {
    nf7::Future<std::shared_ptr<Product>>::Promise pro {ctx};
    ctx->env().ExecGL(ctx, [ctx, pro, t = type_]() mutable {
      pro.Return(std::make_shared<Product>(ctx, GLuint {0}, FromType(t)));
    });
    return pro.future();
  }

  bool Handle(const std::shared_ptr<nf7::Node::Lambda>&             handler,
              const nf7::Mutex::Resource<std::shared_ptr<Product>>& res,
              const nf7::Node::Lambda::Msg&                         in) {
    if (in.name == "upload") {
      const auto& v = in.value;

      const auto vec = v.tuple("vec").vector();

      auto& tex = **res;
      auto& m   = tex.meta();

      uint32_t w = 0, h = 0, d = 0;
      switch (type_) {
      case Type::Tex2D:
      case Type::Rect:
        w = v.tuple("w").integer<uint32_t>();
        h = v.tuple("h").integer<uint32_t>();
        if (w == 0 || h == 0) return false;
        break;
      }
      m.w = w, m.h = h, m.d = d;
      m.format = ToInternalFormat(format_, comp_);

      const auto vecsz =
          w*h*                                        // number of texels
          magic_enum::enum_integer(comp_)*            // number of color components
          (magic_enum::enum_integer(format_) & 0xF);  // size of a component
      if (vec->size() < static_cast<size_t>(vecsz)) {
        throw nf7::Exception {"vector is too small"};
      }

      const auto type = ToCompType(format_);
      const auto fmt  = ToFormat(comp_);
      handler->env().ExecGL(handler, [handler, res, &tex, &m, type, fmt, vec]() {
        glBindTexture(m.type, tex.id());
        switch (m.type) {
        case GL_TEXTURE_2D:
        case GL_TEXTURE_RECTANGLE:
          glTexImage2D(m.type, 0, m.format,
                       static_cast<GLsizei>(m.w),
                       static_cast<GLsizei>(m.h),
                       0, fmt, type, vec->data());
          break;
        default:
          assert(false);
          break;
        }
        glTexParameteri(m.type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(m.type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(m.type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(m.type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(m.type, 0);
      });
      return true;
    } else {
      throw nf7::Exception {"unknown input: "+in.name};
    }
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    const auto t = magic_enum::enum_name(type_);
    ImGui::Text("type  : %.*s", static_cast<int>(t.size()), t.data());

    const auto f = magic_enum::enum_name(format_);
    ImGui::Text("format: %.*s", static_cast<int>(f.size()), f.data());

    const auto c = magic_enum::enum_name(comp_);
    ImGui::Text("comp  : %.*s (%" PRIu8 " values)",
                static_cast<int>(c.size()), c.data(),
                magic_enum::enum_integer(comp_));

    ImGui::Spacing();
    if (prod) {
      const auto  id = static_cast<intptr_t>(prod->id());
      const auto& m  = prod->meta();
      ImGui::Text("  id: %" PRIiPTR, id);
      ImGui::Text("size: %" PRIu32 " x %" PRIu32 " x %" PRIu32, m.w, m.h, m.d);

      if (m.type == GL_TEXTURE_2D) {
        ImGui::Spacing();
        ImGui::TextUnformatted("preview:");
        ImGui::Image(reinterpret_cast<void*>(id),
                     ImVec2 {static_cast<float>(m.w), static_cast<float>(m.h)});
      }
    }
  }

 private:
  Type    type_   = Type::Rect;
  Format  format_ = Format::U8;
  Comp    comp_   = Comp::RGBA;

  static GLenum FromType(Type t) {
    return
        t == Type::Tex2D? GL_TEXTURE_2D:
        t == Type::Rect?  GL_TEXTURE_RECTANGLE:
        throw 0;
  }
  static GLenum ToCompType(Format c) {
    return
        c == Format::U8?  GL_UNSIGNED_BYTE:
        c == Format::F32? GL_FLOAT:
        throw 0;
  }
  static GLenum ToFormat(Comp f) {
    return
        f == Comp::R?    GL_RED:
        f == Comp::RG?   GL_RG:
        f == Comp::RGB?  GL_RGB:
        f == Comp::RGBA? GL_RGBA:
        throw 0;
  }
  static GLint ToInternalFormat(Format f, Comp c) {
    switch (f) {
    case Format::U8:
      return
          c == Comp::R?    GL_R8:
          c == Comp::RG?   GL_RG8:
          c == Comp::RGB?  GL_RGB8:
          c == Comp::RGBA? GL_RGBA8:
          throw 0;
    case Format::F32:
      return
          c == Comp::R?    GL_R32F:
          c == Comp::RG?   GL_RG32F:
          c == Comp::RGB?  GL_RGB32F:
          c == Comp::RGBA? GL_RGBA32F:
          throw 0;
    }
    throw 0;
  }
};
template <>
struct ObjBase<Texture>::TypeInfo final {
  static inline const nf7::GenericTypeInfo<ObjBase<Texture>> kType = {"GL/Texture", {"nf7::DirItem"}};
};


struct Shader {
 public:
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("OpenGL shader");
  }

  static inline const std::vector<std::string> kInputs  = {};
  static inline const std::vector<std::string> kOutputs = {};

  using Product = nf7::gl::Shader;

  enum class Type {
    Vertex,
    Fragment,
  };

  Shader() = default;
  Shader(const Shader&) = default;
  Shader(Shader&&) = default;
  Shader& operator=(const Shader&) = default;
  Shader& operator=(Shader&&) = default;

  void serialize(auto& ar) {
    ar(type_, src_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "type";
    st << YAML::Value << std::string {magic_enum::enum_name(type_)};
    st << YAML::Key   << "src";
    st << YAML::Value << YAML::Literal << src_;
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }
  void Parse(const std::string& v)
  try {
    const auto yaml = YAML::Load(v);

    const auto new_type = magic_enum::
        enum_cast<Type>(yaml["type"].as<std::string>()).value();
    auto new_src = yaml["src"].as<std::string>();

    type_ = new_type;
    src_  = std::move(new_src);
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {"unknown enum"};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(
      const std::shared_ptr<nf7::Context>& ctx, nf7::Env::Watcher&) noexcept {
    // TODO: preprocessing GLSL source

    nf7::Future<std::shared_ptr<Product>>::Promise pro {ctx};
    ctx->env().ExecGL(ctx, [ctx, pro, type = type_, src = src_]() mutable {
      auto sh = std::make_shared<Product>(ctx, GLuint {0}, FromType(type));
      const GLchar* str = src.c_str();
      glShaderSource(sh->id(), 1, &str, nullptr);
      glCompileShader(sh->id());
      assert(0 == glGetError());

      GLint status;
      glGetShaderiv(sh->id(), GL_COMPILE_STATUS, &status);
      if (status == GL_TRUE) {
        pro.Return(sh);
      } else {
        GLint len;
        glGetShaderiv(sh->id(), GL_INFO_LOG_LENGTH, &len);

        std::string ret(static_cast<size_t>(len), ' ');
        glGetShaderInfoLog(sh->id(), len, nullptr, ret.data());

        pro.Throw<nf7::Exception>(std::move(ret));
      }
    });
    return pro.future();
  }

  bool Handle(const std::shared_ptr<nf7::Node::Lambda>&,
              const nf7::Mutex::Resource<std::shared_ptr<Product>>&,
              const nf7::Node::Lambda::Msg&) {
    return false;
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    const auto t = magic_enum::enum_name(type_);
    ImGui::Text("type: %.*s", static_cast<int>(t.size()), t.data());
    if (prod) {
      ImGui::Text("id  : %zu", static_cast<size_t>(prod->id()));
    }
  }

 private:
  Type type_;
  std::string src_;

  static GLenum FromType(Type t) {
    return
        t == Type::Vertex?   GL_VERTEX_SHADER:
        t == Type::Fragment? GL_FRAGMENT_SHADER:
        throw 0;
  }
};
template <>
struct ObjBase<Shader>::TypeInfo final {
  static inline const nf7::GenericTypeInfo<ObjBase<Shader>> kType = {"GL/Shader", {"nf7::DirItem"}};
};

}
}  // namespace nf7


namespace yas::detail {

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::Buffer::Type);
NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::Buffer::Usage);

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::Texture::Type);
NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::Texture::Format);
NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::Texture::Comp);

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::Shader::Type);

}  // namespace yas::detail
