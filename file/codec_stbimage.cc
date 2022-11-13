#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <stb_image.h>

#include "nf7.hh"

#include "common/generic_type_info.hh"
#include "common/node.hh"
#include "common/pure_node_file.hh"


namespace nf7 {
namespace {

class StbImage final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<StbImage> {
 public:
  static inline nf7::GenericTypeInfo<nf7::PureNodeFile<StbImage>> kType = {
    "Codec/StbImage", {"nf7::DirItem"},
  };
  static inline const nf7::Node::Meta kMeta = {{"input"}, {"image", "error"},};

  using nf7::Node::Lambda::Lambda;
  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    std::string npath;
    uint8_t comp = 4;
    if (in.value.isTuple()) {
      npath = in.value.tuple("npath").string();
      comp  = in.value.
          tupleOr("comp", static_cast<nf7::Value::Integer>(comp)).
          integerOrScalar<uint8_t>();
    } else {
      npath = in.value.string();
    }
    env().ExecAsync(shared_from_this(), [=, this]() {
      std::optional<nf7::Value> ret;
      try {
        ret = Exec(npath, comp);
      } catch (nf7::Exception& e) {
        log_->Error(e);
      }
      env().ExecSub(shared_from_this(), [this, in, ret]() {
        if (ret) {
          in.sender->Handle("image", *ret, shared_from_this());
        } else {
          in.sender->Handle("error", nf7::Value::Pulse {}, shared_from_this());
        }
      });
    });
  } catch (nf7::Exception& e) {
    log_->Error(e);
  }
  nf7::Value Exec(const std::string& npath, uint8_t comp) {
    if (comp <= 0 || 4 < comp) {
      throw nf7::Exception {"invalid comp (0~4 are allwoed)"};
    }

    int w, h, actual_comp;
    uint8_t* data = stbi_load(npath.c_str(), &w, &h, &actual_comp, comp);
    if (!data) {
      throw nf7::Exception {"failed to load image from "+npath};
    }
    if (comp != 0) {
      actual_comp = comp;
    }

    const auto size =
        static_cast<size_t>(w)*
        static_cast<size_t>(h)*
        static_cast<size_t>(actual_comp);
    std::vector<uint8_t> buf(size);
    std::memcpy(buf.data(), data, size);
    stbi_image_free(data);
    return std::vector<nf7::Value::TuplePair> {
      {"buf",  std::move(buf)},
      {"w",    static_cast<nf7::Value::Integer>(w)},
      {"h",    static_cast<nf7::Value::Integer>(h)},
      {"comp", static_cast<nf7::Value::Integer>(actual_comp)},
    };
;
  }

  std::shared_ptr<nf7::LoggerRef> log_;
  static_assert(PureNodeFile_LoggerRef<StbImage>);
};

}
}  // namespace nf7
