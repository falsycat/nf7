#include <algorithm>
#include <array>
#include <cinttypes>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <magic_enum.hpp>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/array.hpp>
#include <yas/types/std/optional.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/aggregate_promise.hh"
#include "common/dir_item.hh"
#include "common/factory.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/generic_watcher.hh"
#include "common/gl_enum.hh"
#include "common/gl_fence.hh"
#include "common/gl_obj.hh"
#include "common/gl_shader_preproc.hh"
#include "common/gui_config.hh"
#include "common/gui_window.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/nfile_watcher.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_enum.hh"


using namespace std::literals;

namespace nf7 {
namespace {

struct CreateParam {
  nf7::File*                      file;
  std::shared_ptr<nf7::LoggerRef> log;

  std::shared_ptr<nf7::Context>      ctx;
  std::shared_ptr<nf7::NFileWatcher> nwatch;
  std::shared_ptr<nf7::Env::Watcher> watch;
};
template <typename T>
struct HandleParam {
  nf7::File*                      file;
  std::shared_ptr<nf7::LoggerRef> log;

  std::shared_ptr<nf7::Node::Lambda>       la;
  nf7::Node::Lambda::Msg                   in;
  nf7::Mutex::Resource<std::shared_ptr<T>> obj;
};

template <typename T>
concept HasWindow = requires(T& x) {
  x.UpdateWindow(std::optional<nf7::Future<std::shared_ptr<typename T::Product>>> {});
};

template <typename T>
class ObjBase : public nf7::FileBase,
    public nf7::DirItem, public nf7::Node,
    public nf7::AsyncFactory<nf7::Mutex::Resource<std::shared_ptr<typename T::Product>>> {
 public:
  using ThisObjBase     = ObjBase<T>;
  using Product         = typename T::Product;
  using Resource        = nf7::Mutex::Resource<std::shared_ptr<Product>>;
  using ResourceFuture  = nf7::Future<Resource>;
  using ResourcePromise = typename ResourceFuture::Promise;
  using ThisFactory     = nf7::AsyncFactory<Resource>;

  struct TypeInfo;

  static void UpdateTypeTooltip() noexcept {
    T::UpdateTypeTooltip();
  }

  ObjBase(nf7::Env& env, T&& data = {}) noexcept :
      nf7::FileBase(TypeInfo::kType, env, {}),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kNone),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      nwatch_(std::make_shared<nf7::NFileWatcher>()),
      mem_(std::move(data), *this) {
    nf7::FileBase::Install(*log_);
    nf7::FileBase::Install(*nwatch_);

    nwatch_->onMod = mem_.onRestore = mem_.onCommit = [this]() {
      Drop();
    };

    if constexpr (HasWindow<T>) {
      win_.emplace(*this, T::kWindowTitle);
    }
  }

  ObjBase(nf7::Deserializer& ar) : ObjBase(ar.env()) {
    ar(mem_.data());
    if constexpr (HasWindow<T>) {
      ar(*win_);
    }
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
    if constexpr (HasWindow<T>) {
      ar(*win_);
    }
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
        watch_ = std::make_shared<nf7::GenericWatcher>(env());
        watch_->AddHandler(nf7::File::Event::kUpdate, [self = life_.ref()](auto&) {
          if (self) self->Drop();
        });
        nwatch_->Clear();

        fu_ = mem_->Create(CreateParam {
          .file   = this,
          .log    = log_,
          .ctx    = ctx,
          .nwatch = nwatch_,
          .watch  = watch_,
        });
      }
      fu_->Chain(pro, [k](auto& obj) { return Resource {k, obj}; });
    });
    return pro.future().
        template Catch<nf7::Exception>(ctx, [this](auto& e) {
          log_->Error(e);
        });
  }


  void Update() noexcept override {
    if constexpr (HasWindow<T>) {
      if (win_->shownInCurrentFrame()) {
        const auto em = ImGui::GetFontSize();
        ImGui::SetNextWindowSize({8*em, 8*em}, ImGuiCond_FirstUseEver);
      }
      if (win_->Begin()) {
        mem_->UpdateWindow(fu_);
      }
      win_->End();
    }
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
    if constexpr (HasWindow<T>) {
      ImGui::Separator();
      ImGui::MenuItem(T::kWindowTitle, nullptr, &win_->shown());
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
        nf7::DirItem, nf7::Memento, nf7::Node, ThisFactory>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<ThisObjBase>          life_;
  std::shared_ptr<nf7::LoggerRef> log_;

  std::shared_ptr<nf7::GenericWatcher> watch_;
  std::shared_ptr<nf7::NFileWatcher>   nwatch_;

  nf7::Mutex mtx_;

  std::optional<nf7::Future<std::shared_ptr<Product>>> fu_;

  nf7::GenericMemento<T> mem_;
  std::optional<nf7::gui::Window> win_;


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
              f_.EnforceAlive();
              const auto mod = f_->mem_->Handle(HandleParam<Product> {
                .file = &*f_,
                .log  = f_->log_,
                .la   = shared_from_this(),
                .in   = in,
                .obj  = obj,
              });
              if (mod) f_->Touch();
            } catch (nf7::Exception& e) {
              f_->log_->Error(e);
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

  Buffer() = default;
  Buffer(const Buffer&) = default;
  Buffer(Buffer&&) = default;
  Buffer& operator=(const Buffer&) = default;
  Buffer& operator=(Buffer&&) = default;

  void serialize(auto& ar) {
    ar(target_, usage_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "target";
    st << YAML::Value << std::string {magic_enum::enum_name(target_)};
    st << YAML::Key   << "usage";
    st << YAML::Value << std::string {magic_enum::enum_name(usage_)};
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }

  void Parse(const std::string& v)
  try {
    const auto yaml = YAML::Load(v);

    const auto target = magic_enum::
        enum_cast<gl::BufferTarget>(yaml["target"].as<std::string>()).value();
    const auto usage = magic_enum::
        enum_cast<gl::BufferUsage>(yaml["usage"].as<std::string>()).value();

    target_ = target;
    usage_  = usage;
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {"unknown enum"};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(const CreateParam& p) noexcept {
    const Product::Meta meta {
      .target = target_,
    };
    return meta.Create(p.ctx);
  }

  bool Handle(const HandleParam<Product>& p) {
    if (p.in.name == "upload") {
      const auto& vec   = p.in.value.vector();
      const auto  usage = gl::ToEnum(usage_);

      if (vec->size() == 0) return false;

      p.la->env().ExecGL(p.la, [=]() {
        const auto n = static_cast<GLsizeiptr>(vec->size());

        auto& buf = **p.obj;
        const auto t = gl::ToEnum(buf.meta().target);
        glBindBuffer(t, buf.id());
        {
          auto& size = buf.param().size;
          if (size != vec->size()) {
            size = vec->size();
            glBufferData(t, n, vec->data(), usage);
          } else {
            glBufferSubData(t, 0, n, vec->data());
          }
        }
        glBindBuffer(t, 0);
        assert(0 == glGetError());
      });
      return true;
    } else {
      throw nf7::Exception {"unknown input: "+p.in.name};
    }
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    const auto t = magic_enum::enum_name(target_);
    ImGui::Text("target: %.*s", static_cast<int>(t.size()), t.data());
    if (prod) {
      ImGui::Spacing();
      ImGui::Text("  id: %zu", static_cast<size_t>(prod->id()));
      ImGui::Text("size: %zu bytes", prod->param().size);
    }
  }

 private:
  gl::BufferTarget target_ = gl::BufferTarget::Array;
  gl::BufferUsage  usage_  = gl::BufferUsage::StaticDraw;
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
    "upload", "download",
  };
  static inline const std::vector<std::string> kOutputs = {
    "buffer",
  };

  using Product = nf7::gl::Texture;

  Texture() = default;
  Texture(const Texture&) = default;
  Texture(Texture&&) = default;
  Texture& operator=(const Texture&) = default;
  Texture& operator=(Texture&&) = default;

  void serialize(auto& ar) {
    ar(target_, ifmt_, size_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "target";
    st << YAML::Value << std::string {magic_enum::enum_name(target_)};
    st << YAML::Key   << "ifmt";
    st << YAML::Value << std::string {magic_enum::enum_name(ifmt_)};
    st << YAML::Key   << "size";
    st << YAML::Value << YAML::Flow;
    st << YAML::BeginSeq;
    st << size_[0];
    st << size_[1];
    st << size_[2];
    st << YAML::EndSeq;
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }

  void Parse(const std::string& v)
  try {
    const auto yaml = YAML::Load(v);

    const auto target = magic_enum::
        enum_cast<gl::TextureTarget>(yaml["target"].as<std::string>()).value();
    const auto ifmt = magic_enum::
        enum_cast<gl::InternalFormat>(yaml["ifmt"].as<std::string>()).value();
    const auto size = yaml["size"].as<std::vector<uint32_t>>();

    const auto dim = gl::GetDimension(target);
    const auto itr = std::find(size.begin(), size.end(), 0);
    if (dim > std::distance(size.begin(), itr)) {
      throw nf7::Exception {"invalid size specification"};
    }

    target_  = target;
    ifmt_    = ifmt;

    std::copy(size.begin(), size.begin()+dim, size_.begin());
    std::fill(size_.begin()+dim, size_.end(), 1);
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {"unknown enum"};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(const CreateParam& p) noexcept
  try {
    Product::Meta meta {
      .target = target_,
      .format = ifmt_,
      .size   = {},
    };
    std::transform(size_.begin(), size_.end(), meta.size.begin(),
                   [](auto x) { return static_cast<GLsizei>(x); });
    return meta.Create(p.ctx);
  } catch (nf7::Exception&) {
    return {std::current_exception()};
  }

  bool Handle(const HandleParam<Product>& p) {
    if (p.in.name == "upload") {
      const auto& v = p.in.value;

      const auto vec = v.tuple("vec").vector();
      auto& tex = **p.obj;

      static const char* kOffsetNames[] = {"x", "y", "z"};
      static const char* kSizeNames[]   = {"w", "h", "d"};
      std::array<uint32_t, 3> offset = {0};
      std::array<uint32_t, 3> size   = {1, 1, 1};

      const auto dim = gl::GetDimension(target_);
      for (size_t i = 0; i < dim; ++i) {
        offset[i] = v.tupleOr(kOffsetNames[i], nf7::Value::Integer {0}).integer<uint32_t>();
        size[i]   = v.tuple(kSizeNames[i]).integer<uint32_t>();
        if (size[i] == 0) {
          return false;
        }
        if (offset[i]+size[i] > size_[i]) {
          throw nf7::Exception {"texture size overflow"};
        }
      }

      const auto texel = std::accumulate(size.begin(), size.end(), 1, std::multiplies<uint32_t> {});
      const auto vecsz = texel*gl::GetByteSize(ifmt_);
      if (vec->size() < static_cast<size_t>(vecsz)) {
        throw nf7::Exception {"vector is too small"};
      }

      const auto fmt  = gl::ToEnum(gl::GetColorComp(ifmt_));
      const auto type = gl::ToEnum(gl::GetNumericType(ifmt_));
      p.la->env().ExecGL(p.la, [=, &tex]() {
        const auto t = gl::ToEnum(tex.meta().target);
        glBindTexture(t, tex.id());
        switch (t) {
        case GL_TEXTURE_2D:
        case GL_TEXTURE_RECTANGLE:
          glTexSubImage2D(t, 0,
                          static_cast<GLint>(offset[0]),
                          static_cast<GLint>(offset[1]),
                          static_cast<GLsizei>(size[0]),
                          static_cast<GLsizei>(size[1]),
                          fmt, type, vec->data());
          break;
        default:
          assert(false);
          break;
        }
        glBindTexture(t, 0);
        assert(0 == glGetError());
      });
      return true;

    } else if (p.in.name == "download") {
      auto numtype = gl::GetNumericType(ifmt_);
      auto comp    = gl::GetColorComp(ifmt_);
      try {
        try {
          numtype = magic_enum::enum_cast<
              gl::NumericType>(p.in.value.tuple("numtype").string()).value();
        } catch (nf7::Exception&) {
        }
        try {
          comp = magic_enum::enum_cast<
              gl::ColorComp>(p.in.value.tuple("comp").string()).value();
        } catch (nf7::Exception&) {
        }
      } catch (std::bad_optional_access&) {
        throw nf7::Exception {"unknown enum"};
      }

      p.la->env().ExecGL(p.la, [=]() {
        GLuint pbo;
        glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

        const auto& tex   = **p.obj;
        const auto  size  = tex.meta().size;
        const auto  texel = std::accumulate(size.begin(), size.end(), 1, std::multiplies<uint32_t> {});
        const auto  bsize = texel*gl::GetCompCount(comp)*gl::GetByteSize(numtype);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bsize), nullptr, GL_STREAM_READ);

        const auto t = gl::ToEnum(tex.meta().target);
        glBindTexture(t, tex.id());
        glGetTexImage(t, 0, gl::ToEnum(comp), gl::ToEnum(numtype), nullptr);
        glBindTexture(t, 0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        assert(0 == glGetError());

        nf7::gl::ExecFenceSync(p.la).ThenIf([=, &tex](auto&) {
          glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

          auto buf = std::make_shared<std::vector<uint8_t>>(bsize);

          const auto ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
          std::memcpy(buf->data(), ptr, static_cast<size_t>(bsize));
          glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

          glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
          glDeleteBuffers(1, &pbo);
          assert(0 == glGetError());

          p.la->env().ExecSub(p.la, [=, &tex]() {
            auto v = nf7::Value {std::vector<nf7::Value::TuplePair> {
              {"w",      static_cast<nf7::Value::Integer>(size[0])},
              {"h",      static_cast<nf7::Value::Integer>(size[1])},
              {"d",      static_cast<nf7::Value::Integer>(size[2])},
              {"vector", buf},
            }};
            p.in.sender->Handle("buffer", std::move(v), p.la);
          });
        });
      });
      return false;
    } else {
      throw nf7::Exception {"unknown input: "+p.in.name};
    }
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    const auto t = magic_enum::enum_name(target_);
    ImGui::Text("target: %.*s", static_cast<int>(t.size()), t.data());

    const auto c = magic_enum::enum_name(ifmt_);
    ImGui::Text("ifmt  : %.*s",
                static_cast<int>(c.size()), c.data());

    ImGui::Text("size  : %" PRIu32 " x %" PRIu32 " x %" PRIu32, size_[0], size_[1], size_[2]);

    ImGui::Spacing();
    if (prod) {
      const auto  id = static_cast<intptr_t>(prod->id());
      const auto& m  = prod->meta();
      ImGui::Text("id: %" PRIiPTR, id);

      if (m.target == gl::TextureTarget::Tex2D) {
        ImGui::Spacing();
        ImGui::TextUnformatted("preview:");
        ImGui::Image(reinterpret_cast<void*>(id),
                     ImVec2 {static_cast<float>(size_[0]), static_cast<float>(size_[1])});
      }
    }
  }

  static constexpr const char* kWindowTitle = "Texture Viewer";
  void UpdateWindow(const std::optional<nf7::Future<std::shared_ptr<Product>>>& fu) noexcept {
    if (!fu) {
      ImGui::TextUnformatted("this object is not used yet");
      return;
    }
    if (fu->error()) {
      ImGui::TextUnformatted("error while texture creation ;(");
      return;
    }
    if (fu->yet()) {
      ImGui::TextUnformatted("creating new texture... X)");
      return;
    }
    assert(fu->done());

    const auto& tex = *fu->value();
    if (tex.meta().target != gl::TextureTarget::Tex2D) {
      ImGui::TextUnformatted("only Tex2D texture is supported");
      return;
    }

    const auto avail  = ImGui::GetContentRegionAvail();
    const auto aspect =
        static_cast<float>(tex.meta().size[0]) /
        static_cast<float>(tex.meta().size[1]);

    auto size = ImVec2 {avail.x, avail.x/aspect};
    if (size.y > avail.y) {
      size = ImVec2 {avail.y*aspect, avail.y};
    }

    const auto id = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex.id()));
    ImGui::SetCursorPos(ImGui::GetCursorPos()+(avail-size)/2);
    ImGui::Image(id, size);
  }

 private:
  gl::TextureTarget target_ = gl::TextureTarget::Rect;
  gl::InternalFormat ifmt_  = gl::InternalFormat::RGBA8;

  std::array<uint32_t, 3> size_ = {256, 256, 1};
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

    const auto type = magic_enum::
        enum_cast<gl::ShaderType>(yaml["type"].as<std::string>()).value();
    auto src = yaml["src"].as<std::string>();

    type_ = type;
    src_  = std::move(src);
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {"unknown enum"};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(const CreateParam& p) noexcept {
    nf7::Future<std::shared_ptr<Product>>::Promise pro {p.ctx};

    auto ost  = std::make_shared<std::ostringstream>();
    auto ist  = std::make_shared<std::istringstream>(src_);
    auto path = p.ctx->env().npath() / "INLINE_TEXT";

    auto preproc = std::make_shared<gl::ShaderPreproc>(p.ctx, ost, ist, std::move(path));
    preproc->ExecProcess();
    preproc->future().Chain(p.ctx, pro, [=, type = type_](auto&) mutable {
      const Product::Meta meta {
        .type = type,
      };
      meta.Create(p.ctx, ost->str()).Chain(pro);
    });
    return pro.future().
        ThenIf(p.ctx, [=](auto&) mutable {
          for (const auto& npath : preproc->nfiles()) {
            p.nwatch->Watch(npath);
          }
        });
  }

  bool Handle(const HandleParam<Product>&) {
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
  gl::ShaderType type_;
  std::string src_;
};
template <>
struct ObjBase<Shader>::TypeInfo final {
  static inline const nf7::GenericTypeInfo<ObjBase<Shader>> kType = {"GL/Shader", {"nf7::DirItem"}};
};


struct Program {
 public:
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("OpenGL program");
  }

  static inline const std::vector<std::string> kInputs  = {
    "draw",
  };
  static inline const std::vector<std::string> kOutputs = {
    "done",
  };

  using Product = nf7::gl::Program;

  Program() = default;
  Program(const Program&) = default;
  Program(Program&&) = default;
  Program& operator=(const Program&) = default;
  Program& operator=(Program&&) = default;

  void serialize(auto& ar) {
    ar(shaders_, depth_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "shaders";
    st << YAML::Value << YAML::BeginSeq;
    for (const auto& shader : shaders_) {
      st << shader.Stringify();
    }
    st << YAML::EndSeq;

    if (depth_) {
      st << YAML::Key << "depth";
      st << YAML::BeginMap;
      st << YAML::Key   << "near";
      st << YAML::Value << depth_->near;
      st << YAML::Key   << "far";
      st << YAML::Value << depth_->far;
      st << YAML::Key   << "func";
      st << YAML::Value << std::string {magic_enum::enum_name(depth_->func)};
      st << YAML::EndMap;
    }
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }
  void Parse(const std::string& v)
  try {
    const auto yaml = YAML::Load(v);

    Program ret;
    for (const auto& shader : yaml["shaders"]) {
      ret.shaders_.push_back(
          nf7::File::Path::Parse(shader.as<std::string>()));
    }
    if (ret.shaders_.size() == 0) {
      throw nf7::Exception {"no shader is attached"};
    }

    if (const auto& yaml_depth = yaml["depth"]) {
      depth_.emplace(Product::Meta::Depth {
        .near = yaml_depth["near"].as<float>(),
        .far  = yaml_depth["far"].as<float>(),
        .func = magic_enum::enum_cast<gl::TestFunc>(
            yaml_depth["func"].as<std::string>()).value(),
      });
    }

    *this = std::move(ret);
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(const CreateParam& p) noexcept
  try {
    auto& base = *p.file;

    std::vector<nf7::File::Id> shaders;
    for (const auto& path : shaders_) {
      const auto fid = base.ResolveOrThrow(path).id();
      p.watch->Watch(fid);
      shaders.push_back(fid);
    }
    return Product::Meta().Create(p.ctx, shaders);
  } catch (nf7::Exception&) {
    return {std::current_exception()};
  }

  bool Handle(const HandleParam<Product>& p) {
    const auto& base = *p.file;
    const auto& v    = p.in.value;

    if (p.in.name == "draw") {
      const auto mode  = gl::ToEnum<gl::DrawMode>(v.tuple("mode").string());
      const auto count = v.tuple("count").integer<GLsizei>();
      const auto inst  = v.tupleOr("instance", nf7::Value::Integer{1}).integer<GLsizei>();

      const auto uni = v.tupleOr("uniform", nf7::Value::Tuple {}).tuple();
      const auto tex = v.tupleOr("texture", nf7::Value::Tuple {}).tuple();
      if (tex->size() > GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS) {
        throw nf7::Exception {"too many textures specified"};
      }

      const auto& vp = v.tuple("viewport");
      const auto vp_x = vp.tuple(0).integerOrScalar<GLint>();
      const auto vp_y = vp.tuple(1).integerOrScalar<GLint>();
      const auto vp_w = vp.tuple(2).integerOrScalar<GLsizei>();
      const auto vp_h = vp.tuple(3).integerOrScalar<GLsizei>();
      if (vp_w < 0 || vp_h < 0) {
        throw nf7::Exception {"negative size viewport"};
      }

      gl::Program::Meta config = (**p.obj).meta();
      // TODO: override configurations

      // this will be triggered when all preparation done
      nf7::AggregatePromise apro {p.la};

      // find, fetch and lock FBO
      std::optional<nf7::gl::FramebufferFactory::Product>                fbo_fu;
      std::optional<nf7::gl::Framebuffer::Meta::LockedAttachmentsFuture> fbo_lock_fu;
      {
        fbo_fu = base.
            ResolveOrThrow(v.tuple("fbo").string()).
            interfaceOrThrow<nf7::gl::FramebufferFactory>().Create();

        nf7::gl::Framebuffer::Meta::LockedAttachmentsFuture::Promise fbo_lock_pro;
        fbo_fu->ThenIf([la = p.la, fbo_lock_pro](auto& fbo) mutable {
          (**fbo).meta().LockAttachments(la).Chain(fbo_lock_pro);
        });

        fbo_lock_fu = fbo_lock_pro.future();
        apro.Add(*fbo_lock_fu);
      }

      // find, fetch and lock VAO
      std::optional<nf7::gl::VertexArrayFactory::Product>            vao_fu;
      std::optional<nf7::gl::VertexArray::Meta::LockedBuffersFuture> vao_lock_fu;
      {
        vao_fu = base.
            ResolveOrThrow(v.tuple("vao").string()).
            interfaceOrThrow<nf7::gl::VertexArrayFactory>().Create();

        nf7::gl::VertexArray::Meta::ValidationHint vhint;
        vhint.vertices  = static_cast<size_t>(count);
        vhint.instances = static_cast<size_t>(inst);

        nf7::gl::VertexArray::Meta::LockedBuffersFuture::Promise vao_lock_pro;
        vao_fu->ThenIf([la = p.la, vao_lock_pro, vhint](auto& vao) mutable {
          (**vao).meta().LockBuffers(la, vhint).Chain(vao_lock_pro);
        });

        vao_lock_fu = vao_lock_pro.future();
        apro.Add(*vao_lock_fu);
      }

      // find, fetch and lock textures
      std::vector<std::pair<std::string, nf7::gl::TextureFactory::Product>> tex_fu;
      tex_fu.reserve(tex->size());
      for (auto& pa : *tex) {
        auto fu = base.
            ResolveOrThrow(pa.second.string()).
            interfaceOrThrow<nf7::gl::TextureFactory>().
            Create();
        tex_fu.emplace_back(pa.first, fu);
        apro.Add(fu);
      }

      // execute drawing after successful locking
      apro.future().Then(nf7::Env::kGL, p.la, [=, tex_fu = std::move(tex_fu)](auto&) {
        assert(fbo_lock_fu);
        assert(vao_lock_fu);
        try {
          if (fbo_lock_fu->error()) fbo_lock_fu->value();
          if (fbo_lock_fu->error()) vao_lock_fu->value();
        } catch (nf7::Exception&) {
          p.log->Error("failed to acquire lock of VAO or FBO");
          return;
        }
        const auto& fbo  = *fbo_fu->value();
        const auto& vao  = *vao_fu->value();
        const auto& prog = *p.obj;

        // bind objects
        glUseProgram(prog->id());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo->id());
        glBindVertexArray(vao->id());
        glViewport(vp_x, vp_y, vp_w, vp_h);

        // setup uniforms
        for (const auto& pa : *uni) {
          try {
            SetUniform(prog->id(), pa.first.c_str(), pa.second);
          } catch (nf7::Exception& e) {
            p.log->Warn("uniform '"+pa.first+"' is ignored");
          }
        }

        // bind textures
        for (size_t i = 0; i < tex_fu.size(); ++i) {
          const auto& pa = tex_fu[i];
          try {
            const GLint loc = glGetUniformLocation(prog->id(), pa.first.c_str());
            if (loc < 0) {
              throw nf7::Exception {"missing uniform to bind texture"};
            }
            const auto& tex = *pa.second.value();
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
            glBindTexture(gl::ToEnum(tex->meta().target), tex->id());
            glUniform1i(loc, static_cast<GLint>(i));
          } catch (nf7::Exception&) {
            p.log->Warn("texture '"+pa.first+"' is ignored");
          }
        }

        // draw
        config.ApplyState();
        if (vao->meta().index) {
          const auto numtype = gl::ToEnum(vao->meta().index->numtype);
          glDrawElementsInstanced(mode, count, numtype, nullptr, inst);
        } else {
          glDrawArraysInstanced(mode, 0, count, inst);
        }
        config.RevertState();
        const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

        // unbind all
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glUseProgram(0);
        assert(0 == glGetError());

        if (status != GL_FRAMEBUFFER_COMPLETE) {
          p.log->Warn("framebuffer is broken");
        }
      });
      return false;
    } else {
      throw nf7::Exception {"unknown input: "+p.in.name};
    }
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    if (prod) {
      ImGui::Text("id  : %zu", static_cast<size_t>(prod->id()));
    }
  }

 private:
  std::vector<nf7::File::Path>        shaders_;
  std::optional<Product::Meta::Depth> depth_ = {{}};


  static void SetUniform(GLuint prog, const char* name, const nf7::Value& v) {
    assert(0 == glGetError());
    const GLint loc = glGetUniformLocation(prog, name);
    if (loc < 0) {
      throw nf7::Exception {"unknown uniform identifier"};
    }

    // single integer
    try {
      glUniform1i(loc, v.integer<GLint>());
      return;
    } catch (nf7::Exception&) {
    }

    // single float
    try {
      glUniform1f(loc, v.scalar<GLfloat>());
      return;
    } catch (nf7::Exception&) {
    }
  
    // 1~4 dim float vector
    try {
      const auto& tup = *v.tuple();
      switch (tup.size()) {
      case 1: glUniform1f(loc, tup[0].second.scalar<GLfloat>()); break;
      case 2: glUniform2f(loc, tup[0].second.scalar<GLfloat>(),
                               tup[1].second.scalar<GLfloat>()); break;
      case 3: glUniform3f(loc, tup[0].second.scalar<GLfloat>(),
                               tup[1].second.scalar<GLfloat>(),
                               tup[2].second.scalar<GLfloat>()); break;
      case 4: glUniform4f(loc, tup[0].second.scalar<GLfloat>(),
                               tup[1].second.scalar<GLfloat>(),
                               tup[2].second.scalar<GLfloat>(),
                               tup[3].second.scalar<GLfloat>()); break;
      default: throw nf7::Exception {"invalid tuple size (must be 1~4)"};
      }
      return;
    } catch (nf7::Exception&) {
    }

    throw nf7::Exception {"the value is not compatible with any uniform type"};
  }
};
template <>
struct ObjBase<Program>::TypeInfo final {
  static inline const nf7::GenericTypeInfo<ObjBase<Program>> kType = {"GL/Program", {"nf7::DirItem"}};
};


struct VertexArray {
 public:
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("OpenGL Vertex Array Object");
  }

  static inline const std::vector<std::string> kInputs  = {
  };
  static inline const std::vector<std::string> kOutputs = {
  };

  using Product = nf7::gl::VertexArray;

  struct Attr {
    GLuint          location  = 0;
    GLint           size      = 1;
    gl::NumericType type      = gl::NumericType::F32;
    bool            normalize = false;
    GLsizei         stride    = 0;
    uint64_t        offset    = 0;
    GLuint          divisor   = 0;
    nf7::File::Path buffer    = {};

    void serialize(auto& ar) {
      ar(location, size, type, normalize, stride, offset, divisor, buffer);
    }

    const char* Validate() const noexcept {
      if (location >= GL_MAX_VERTEX_ATTRIBS) {
        return "too huge location";
      }
      if (size <= 0 || 4 < size) {
        return "invalid size (1, 2, 3 or 4 are allowed)";
      }
      if (stride < 0) {
        return "negative stride";
      }
      if (offset > static_cast<uint64_t>(stride)) {
        return "offset overflow";
      }
      return nullptr;
    }
    static void Validate(std::span<const Attr> attrs) {
      for (auto& attr : attrs) {
        if (const auto msg = attr.Validate()) {
          throw nf7::Exception {"invalid attribute: "s+msg};
        }
      }
      std::unordered_set<GLuint> idx;
      for (auto& attr : attrs) {
        const auto [itr, uniq] = idx.insert(attr.location);
        (void) itr;
        if (!uniq) {
          throw nf7::Exception {"attribute location duplicated"};
        }
      }
    }
  };

  VertexArray() = default;
  VertexArray(const VertexArray&) = default;
  VertexArray(VertexArray&&) = default;
  VertexArray& operator=(const VertexArray&) = default;
  VertexArray& operator=(VertexArray&&) = default;

  void serialize(auto& ar) {
    ar(index_, index_numtype_, attrs_);
    Attr::Validate(attrs_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "index";
    st << YAML::BeginMap;
    st << YAML::Key   << "buffer";
    st << YAML::Value << index_.Stringify();
    st << YAML::Key   << "type";
    st << YAML::Value << std::string {magic_enum::enum_name(index_numtype_)};
    st << YAML::EndMap;
    st << YAML::Key   << "attrs";
    st << YAML::Value << YAML::BeginSeq;
    for (const auto& attr : attrs_) {
      st << YAML::BeginMap;
      st << YAML::Key   << "location";
      st << YAML::Value << attr.location;
      st << YAML::Key   << "size";
      st << YAML::Value << attr.size;
      st << YAML::Key   << "type";
      st << YAML::Value << std::string {magic_enum::enum_name(attr.type)};
      st << YAML::Key   << "normalize";
      st << YAML::Value << attr.normalize;
      st << YAML::Key   << "stride";
      st << YAML::Value << attr.stride;
      st << YAML::Key   << "offset";
      st << YAML::Value << attr.offset;
      st << YAML::Key   << "divisor";
      st << YAML::Value << attr.divisor;
      st << YAML::Key   << "buffer";
      st << YAML::Value << attr.buffer.Stringify();
      st << YAML::EndMap;
    }
    st << YAML::EndSeq;
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }
  void Parse(const std::string& v)
  try {
    const auto yaml = YAML::Load(v);

    auto index = nf7::File::Path::Parse(yaml["index"]["buffer"].as<std::string>());

    const auto index_numtype = magic_enum::enum_cast<gl::NumericType>(
        yaml["index"]["type"].as<std::string>()).value();

    std::vector<Attr> attrs;
    for (const auto& attr : yaml["attrs"]) {
      attrs.push_back({
        .location  = attr["location"].as<GLuint>(),
        .size      = attr["size"].as<GLint>(),
        .type      = magic_enum::enum_cast<gl::NumericType>(attr["type"].as<std::string>()).value(),
        .normalize = attr["normalize"].as<bool>(),
        .stride    = attr["stride"].as<GLsizei>(),
        .offset    = attr["offset"].as<uint64_t>(),
        .divisor   = attr["divisor"].as<GLuint>(),
        .buffer    = nf7::File::Path::Parse(attr["buffer"].as<std::string>()),
      });
    }
    Attr::Validate(attrs);

    index_         = std::move(index);
    index_numtype_ = index_numtype;
    attrs_         = std::move(attrs);
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {std::string {"invalid enum"}};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(const CreateParam& p) noexcept
  try {
    auto& base = *p.file;
    Product::Meta meta;

    if (index_.terms().size() > 0) {
      const auto fid = base.ResolveOrThrow(index_).id();
      p.watch->Watch(fid);

      meta.index.emplace();
      meta.index->buffer  = fid;
      meta.index->numtype = index_numtype_;
    }

    meta.attrs.reserve(attrs_.size());
    for (auto& attr : attrs_) {
      const auto fid = base.ResolveOrThrow(attr.buffer).id();
      p.watch->Watch(fid);
      meta.attrs.push_back({
        .buffer    = fid,
        .location  = attr.location,
        .size      = attr.size,
        .type      = attr.type,
        .normalize = attr.normalize,
        .stride    = attr.stride,
        .offset    = attr.offset,
        .divisor   = attr.divisor,
      });
    }
    return meta.Create(p.ctx);
  } catch (nf7::Exception&) {
    return {std::current_exception()};
  }

  bool Handle(const HandleParam<Product>&) {
    return false;
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    if (prod) {
      ImGui::Text("id: %zu", static_cast<size_t>(prod->id()));
    }
  }

 private:
  nf7::File::Path index_;
  gl::NumericType index_numtype_;
  std::vector<Attr> attrs_;
};
template <>
struct ObjBase<VertexArray>::TypeInfo final {
  static inline const nf7::GenericTypeInfo<ObjBase<VertexArray>> kType = {"GL/VertexArray", {"nf7::DirItem"}};
};


struct Framebuffer {
 public:
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("OpenGL Framebuffer Object");
  }

  static inline const std::vector<std::string> kInputs  = {
    "clear",
  };
  static inline const std::vector<std::string> kOutputs = {
  };

  using Product = nf7::gl::Framebuffer;

  struct Attachment {
    nf7::File::Path path;

    void serialize(auto& ar) {
      ar(path);
    }
  };

  Framebuffer() = default;
  Framebuffer(const Framebuffer&) = default;
  Framebuffer(Framebuffer&&) = default;
  Framebuffer& operator=(const Framebuffer&) = default;
  Framebuffer& operator=(Framebuffer&&) = default;

  void serialize(auto& ar) {
    ar(colors_, depth_, stencil_);
  }

  std::string Stringify() noexcept {
    YAML::Emitter st;
    st << YAML::BeginMap;
    st << YAML::Key   << "colors";
    st << YAML::Value << YAML::BeginMap;
    for (size_t i = 0; i < Product::Meta::kColorSlotCount; ++i) {
      if (colors_[i]) {
        st << YAML::Key   << i;
        st << YAML::Value << colors_[i]->path.Stringify();
      }
    }
    st << YAML::EndMap;
    if (depth_) {
      st << YAML::Key << "depth";
      st << YAML::Value << depth_->path.Stringify();
    }
    if (stencil_) {
      st << YAML::Key << "stencil";
      st << YAML::Value << stencil_->path.Stringify();
    }
    st << YAML::EndMap;
    return std::string {st.c_str(), st.size()};
  }
  void Parse(const std::string& v)
  try {
    const auto  yaml        = YAML::Load(v);
    const auto& yaml_colors = yaml["colors"];

    Framebuffer ret;
    for (size_t i = 0; i < Product::Meta::kColorSlotCount; ++i) {
      if (auto& yaml_color = yaml_colors[std::to_string(i)]) {
        ret.colors_[i].emplace(Attachment {
          .path = nf7::File::Path::Parse(yaml_color.as<std::string>()),
        });
      }
    }
    if (const auto& yaml_depth = yaml["depth"]) {
      ret.depth_.emplace(Attachment {
        .path = nf7::File::Path::Parse(yaml_depth.as<std::string>()),
      });
    }
    if (const auto& yaml_stencil = yaml["stencil"]) {
      ret.stencil_.emplace(Attachment {
        .path = nf7::File::Path::Parse(yaml_stencil.as<std::string>()),
      });
    }

    *this = std::move(ret);
  } catch (std::bad_optional_access&) {
    throw nf7::Exception {std::string {"invalid enum"}};
  } catch (YAML::Exception& e) {
    throw nf7::Exception {std::string {"YAML error: "}+e.what()};
  }

  nf7::Future<std::shared_ptr<Product>> Create(const CreateParam& p) noexcept
  try {
    auto& base = *p.file;
    Product::Meta meta;

    const auto resolveAndWatch = [&](const auto& path) {
      const auto fid = base.ResolveOrThrow(path).id();
      p.watch->Watch(fid);
      return fid;
    };

    for (size_t i = 0; i < Product::Meta::kColorSlotCount; ++i) {
      if (const auto& color = colors_[i]) {
        meta.colors[i].emplace(Product::Meta::Attachment {
          .tex = resolveAndWatch(color->path),
        });
      }
    }
    if (depth_) {
      meta.depth.emplace(Product::Meta::Attachment {
        .tex = resolveAndWatch(depth_->path),
      });
    }
    if (stencil_) {
      meta.stencil.emplace(Product::Meta::Attachment {
        .tex = resolveAndWatch(stencil_->path),
      });
    }
    return meta.Create(p.ctx);
  } catch (nf7::Exception&) {
    return {std::current_exception()};
  }

  bool Handle(const HandleParam<Product>& p) {
    if (p.in.name == "clear") {
      (**p.obj).meta().LockAttachments(p.la).ThenIf(nf7::Env::kGL, p.la, [=](auto&) {
        glBindFramebuffer(GL_FRAMEBUFFER, (**p.obj).id());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
      });
      return false;
    } else {
      throw nf7::Exception {"unknown command: "+p.in.name};
    }
  }

  void UpdateTooltip(const std::shared_ptr<Product>& prod) noexcept {
    if (prod) {
      ImGui::Text("id: %zu", static_cast<size_t>(prod->id()));
    }
  }

 private:
  std::array<std::optional<Attachment>, Product::Meta::kColorSlotCount> colors_;
  std::optional<Attachment> depth_;
  std::optional<Attachment> stencil_;
};
template <>
struct ObjBase<Framebuffer>::TypeInfo final {
  static inline const nf7::GenericTypeInfo<ObjBase<Framebuffer>> kType = {"GL/Framebuffer", {"nf7::DirItem"}};
};

}
}  // namespace nf7
