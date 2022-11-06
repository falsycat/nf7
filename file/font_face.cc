#include <array>
#include <cassert>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/factory.hh"
#include "common/file_base.hh"
#include "common/font_face.hh"
#include "common/font_queue.hh"
#include "common/future.hh"
#include "common/generic_config.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/memento.hh"
#include "common/nfile_watcher.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_std_filesystem.hh"


namespace nf7 {
namespace {

class FontFace final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem, public nf7::Node,
    public nf7::AsyncFactory<std::shared_ptr<nf7::font::Face>> {
 public:
  static inline const nf7::GenericTypeInfo<FontFace> kType = {"Font/Face", {"nf7::DirItem",}};

  class Lambda;

  struct Data {
    std::filesystem::path npath;

    void serialize(auto& ar) { ar(npath); }

    std::string Stringify() const noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "npath";
      st << YAML::Value << npath.generic_string();
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str)
    try {
      const auto yaml = YAML::Load(str);
      Data d;
      d.npath = yaml["npath"].as<std::string>();
      *this = d;
    } catch (YAML::Exception& e) {
      throw nf7::Exception {e.what()};
    }
  };

  FontFace(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kNone),
      life_(*this),
      nwatch_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      mem_(*this, std::move(d)) {
    mem_.onCommit = mem_.onRestore = nwatch_.onMod = [this]() {
      cache_ = std::nullopt;
      Touch();
    };
  }

  FontFace(nf7::Deserializer& ar) : FontFace(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<FontFace>(env, Data {mem_.data()});
  }

  nf7::Future<std::shared_ptr<nf7::font::Face>> Create() noexcept override
  try {
    if (cache_) return *cache_;
    auto& q   = ResolveUpwardOrThrow("_font").interfaceOrThrow<nf7::font::Queue>();
    auto  ctx = std::make_shared<nf7::GenericContext>(*this, "font face factory");
    cache_ = nf7::font::Face::Create(ctx, q.self(), mem_->npath);
    return *cache_;
  } catch (nf7::Exception&) {
    return {std::current_exception()};
  }

  std::span<const std::string> GetInputs() const noexcept override {
    static const std::vector<std::string> kInputs = {"command"};
    return kInputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    static const std::vector<std::string> kOutputs = {"result"};
    return kOutputs;
  }
  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<FontFace>             life_;
  nf7::NFileWatcher               nwatch_;
  std::shared_ptr<nf7::LoggerRef> log_;

  nf7::GenericMemento<Data> mem_;

  std::optional<nf7::Future<std::shared_ptr<nf7::font::Face>>> cache_;
};


class FontFace::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<FontFace::Lambda> {
 public:
  Lambda(FontFace& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept
  try {
    f_.EnforceAlive();
    if (in.name == "command") {
      const auto  code     = in.value.tuple("code").integerOrScalar<FT_ULong>();
      const auto& size_tup = in.value.tuple("size");

      std::array<FT_UInt, 2> size = {0, 0};
      if (size_tup.isInteger() || size_tup.isScalar()) {
        size[1] = size_tup.integerOrScalar<FT_UInt>();
      } else {
        for (size_t i = 0; i < size.size(); ++i) {
          size[i] = size_tup.
              tupleOr(i, nf7::Value::Integer {0}).
              integerOrScalar<FT_UInt>();
        }
      }

      auto self = shared_from_this();
      nf7::Future<nf7::Value>::Promise pro {self};

      f_->Create().Chain(pro, [=, this](auto& face) mutable {
        face->ftq()->Push(self, [=, this](auto) mutable {
          pro.Wrap([&]() { return Exec(**face, size, code); });
        });
      });

      pro.future().ThenIf(self, [=](auto& v) {
        in.sender->Handle("result", v, self);
      }).Catch<nf7::Exception>(self, [log = f_->log_](auto& e) {
        log->Error(e);
      });
    } else {
      assert(false);
    }
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception& e) {
    f_->log_->Error(e);
  }

  nf7::Value Exec(FT_Face face, const std::array<FT_UInt, 2>& size, FT_ULong code) {
    font::Enforce(FT_Set_Pixel_Sizes(face, size[0], size[1]));
    font::Enforce(FT_Load_Char(face, code, FT_LOAD_RENDER));

    // check the loaded glyph
    const auto& g = *face->glyph;
    if (g.bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
      throw nf7::Exception {"unsupported pixel mode (only 8-bit grayscale allowed)"};
    }
    if (g.format != FT_GLYPH_FORMAT_BITMAP) {
      throw nf7::Exception {"unsupported glyph format (only bitmap allowed)"};
    }

    // copy rendered bitmap
    std::vector<uint8_t> dst(g.bitmap.width*g.bitmap.rows);
    auto src = g.bitmap.buffer;
    for (unsigned int y = 0; y < g.bitmap.rows; ++y) {
      std::memcpy(&dst[y*g.bitmap.width], src, g.bitmap.width);
      src += g.bitmap.pitch;
    }

    return nf7::Value { std::vector<nf7::Value::TuplePair> {
      {"w",      static_cast<nf7::Value::Integer>(g.bitmap.width)},
      {"h",      static_cast<nf7::Value::Integer>(g.bitmap.rows)},
      {"buf",    std::move(dst)},
      {"hBearX", static_cast<nf7::Value::Integer>(g.metrics.horiBearingX)},
      {"hBearY", static_cast<nf7::Value::Integer>(g.metrics.horiBearingY)},
      {"hAdv",   static_cast<nf7::Value::Integer>(g.metrics.horiAdvance)},
      {"vBearX", static_cast<nf7::Value::Integer>(g.metrics.vertBearingX)},
      {"vBearY", static_cast<nf7::Value::Integer>(g.metrics.vertBearingY)},
      {"vAdv",   static_cast<nf7::Value::Integer>(g.metrics.vertAdvance)},
    }};
  }

 private:
  nf7::Life<FontFace>::Ref f_;
};


std::shared_ptr<nf7::Node::Lambda> FontFace::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<FontFace::Lambda>(*this, parent);
}

void FontFace::UpdateMenu() noexcept {
  if (ImGui::MenuItem("load")) {
    Create();
  }
}
void FontFace::UpdateTooltip() noexcept {
  ImGui::Text("npath : %s", mem_->npath.generic_string().c_str());

  const char* status = "unused";
  if (cache_) {
    status =
        cache_->done()?  "loaded":
        cache_->yet()?   "loading":
        cache_->error()? "broken": "X(";
  }
  ImGui::Text("status: %s", status);
}

}  // namespace
}  // namespace nf7

