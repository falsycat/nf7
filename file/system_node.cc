#include <memory>

#include "nf7.hh"

#include "common/generic_dir.hh"
#include "common/generic_type_info.hh"
#include "common/node.hh"
#include "common/pure_node_file.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class Save final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Save> {
 public:
  static inline nf7::GenericTypeInfo<nf7::PureNodeFile<Save>> kType = {
    "System/Node/Save", {"nf7::DirItem"},
  };
  static inline const nf7::Node::Meta kMeta = { {"exec"}, {}, };

  using nf7::Node::Lambda::Lambda;
  void Handle(const nf7::Node::Lambda::Msg&) noexcept override {
    env().ExecMain(shared_from_this(), [this]() {
      env().Save();
    });
  }
};

class Exit final : public nf7::Node::Lambda {
 public:
  static inline nf7::GenericTypeInfo<nf7::PureNodeFile<Exit>> kType = {
    "System/Node/Exit", {"nf7::DirItem"},
  };
  static inline const nf7::Node::Meta kMeta = { {"exec"}, {}, };

  using nf7::Node::Lambda::Lambda;
  void Handle(const nf7::Node::Lambda::Msg&) noexcept override {
    env().Exit();
  }
};

class Panic final : public nf7::Node::Lambda {
 public:
  static inline nf7::GenericTypeInfo<nf7::PureNodeFile<Panic>> kType = {
    "System/Node/Panic", {"nf7::DirItem"},
  };
  static inline const nf7::Node::Meta kMeta = { {"exec"}, {}, };

  using nf7::Node::Lambda::Lambda;
  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    try {
      if (in.value.isString()) {
        throw nf7::Exception {in.value.string()};
      } else {
        throw nf7::Exception {
          "'panic' input can take a string as message shown here :)"};
      }
    } catch (nf7::Exception&) {
      env().Throw(std::make_exception_ptr<nf7::Exception>({"panic caused by System/Node"}));
    }
  }
};

}  // namespace
}  // namespace nf7
