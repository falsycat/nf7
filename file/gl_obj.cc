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
      fu_->ThenIf([pro, k](auto& obj) mutable { pro.Return({k, obj}); });
    });
    return pro.future();
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

}
}  // namespace nf7


namespace yas::detail {

template <size_t F>
struct serializer<
    yas::detail::type_prop::is_enum,
    yas::detail::ser_case::use_internal_serializer,
    F, nf7::Buffer::Type> :
        nf7::EnumSerializer<nf7::Buffer::Type> {
};

template <size_t F>
struct serializer<
    yas::detail::type_prop::is_enum,
    yas::detail::ser_case::use_internal_serializer,
    F, nf7::Buffer::Usage> :
        nf7::EnumSerializer<nf7::Buffer::Usage> {
};

}  // namespace yas::detail
