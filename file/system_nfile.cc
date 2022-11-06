#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/mutex.hh"
#include "common/nfile.hh"
#include "common/nfile_watcher.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/thread.hh"
#include "common/yas_std_filesystem.hh"


namespace nf7 {
namespace {

class NFile final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<NFile> kType = {
    "System/NFile", {"nf7::DirItem", "nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Read/Write a file placed on native filesystem.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  class Lambda;

  struct SharedData final {
    SharedData(NFile& f) noexcept : log(f) {
    }

    nf7::LoggerRef log;
    std::optional<nf7::NFile> nfile;
  };
  struct Runner final {
    struct Task {
      std::shared_ptr<nf7::Node::Lambda> callee, caller;
      std::function<nf7::Value()> func;
    };

    Runner(const std::shared_ptr<SharedData>& shared) noexcept :
        shared_(shared) {
    }
    void operator()(Task&& t) noexcept
    try {
      auto callee = t.callee;
      auto caller = t.caller;
      auto ret    = t.func();
      if (callee && caller) {
        callee->env().ExecSub(callee, [callee, caller, ret]() {
          caller->Handle("result", ret, callee);
        });
      }
    } catch (nf7::Exception& e) {
      shared_->log.Error("operation failure: "+e.msg());
    }

   private:
    std::shared_ptr<SharedData> shared_;
  };
  using Thread = nf7::Thread<Runner, Runner::Task>;

  struct Data final {
    std::filesystem::path npath;
    std::string mode;

    std::string Stringify() noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "npath";
      st << YAML::Value << npath.generic_string();
      st << YAML::Key   << "mode";
      st << YAML::Value << mode;
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str)
    try {
      const auto yaml = YAML::Load(str);

      Data d;
      d.npath = yaml["npath"].as<std::string>();
      d.mode  = yaml["mode"].as<std::string>();

      *this = std::move(d);
    } catch (YAML::Exception& e) {
      throw nf7::Exception {e.what()};
    }
  };

  NFile(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kMenu_DirItem),
      life_(*this), nwatch_(*this),
      shared_(std::make_shared<SharedData>(*this)),
      th_(std::make_shared<Thread>(*this, Runner {shared_})),
      mem_(std::move(data), *this) {
    mtx_.onLock   = [this]() { SetUp(); };
    mtx_.onUnlock = [this]() { shared_->nfile.reset(); };

    mem_.onRestore = mem_.onCommit = [this]() {
      nwatch_.Clear();
      nwatch_.Watch(mem_->npath);
    };
    nwatch_.onMod = [this]() { Touch(); };
  }

  NFile(nf7::Deserializer& ar) : NFile(ar.env()) {
    ar(mem_->npath, mem_->mode);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_->npath, mem_->mode);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<NFile>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  std::span<const std::string> GetInputs() const noexcept override {
    static const std::vector<std::string> kInputs = {"command"};
    return kInputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    static const std::vector<std::string> kOutputs = {"result"};
    return kOutputs;
  }

  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<NFile>  life_;
  nf7::NFileWatcher nwatch_;

  std::shared_ptr<SharedData> shared_;
  std::shared_ptr<Thread>     th_;
  nf7::Mutex                  mtx_;

  nf7::GenericMemento<Data> mem_;


  void SetUp() {
    const auto& mode = mem_->mode;
    nf7::NFile::Flags flags = 0;
    if (std::string::npos != mode.find('r')) flags |= nf7::NFile::kRead;
    if (std::string::npos != mode.find('w')) flags |= nf7::NFile::kWrite;

    auto ctx = std::make_shared<nf7::GenericContext>(*this);
    th_->Push(ctx, Runner::Task {
      .callee = nullptr,
      .caller = nullptr,
      .func   = [shared = shared_, npath = mem_->npath, flags]() {
        shared->nfile.emplace(npath, flags);
        return nf7::Value::Pulse {};
      },
    });
  }
};

class NFile::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<NFile::Lambda> {
 public:
  Lambda(NFile& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_), shared_(f.shared_) {
  }
  ~Lambda() noexcept {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();

    const auto& v    = in.value;
    const auto  type = v.tuple("type").string();
    if (type == "lock") {
      const auto ex = v.tuple("ex").boolean();
      Push(in.sender, ex, []() { return nf7::Value::Pulse {}; });

    } else if (type == "unlock") {
      lock_ = std::nullopt;
      in.sender->Handle("result", nf7::Value::Pulse {}, shared_from_this());

    } else if (type == "read") {
      const auto offset = v.tuple("offset").integer<size_t>();
      const auto size   = v.tuple("size").integer<size_t>();
      Push(in.sender, false, [this, offset, size]() {
        std::vector<uint8_t> buf;
        buf.resize(size);
        const auto actual = shared_->nfile->Read(offset, buf.data(), size);
        buf.resize(actual);
        return nf7::Value {std::move(buf)};
      });

    } else if (type == "write") {
      const auto offset = v.tuple("offset").integer<size_t>();
      const auto buf    = v.tuple("buf").vector();
      Push(in.sender, true, [this, offset, buf]() {
        const auto ret = shared_->nfile->Write(offset, buf->data(), buf->size());
        return nf7::Value {static_cast<nf7::Value::Integer>(ret)};
      });

    } else if (type == "truncate") {
      const auto size = v.tuple("size").integer<size_t>();
      Push(in.sender, true, [this, size]() {
        shared_->nfile->Truncate(size);
        return nf7::Value::Pulse {};
      });

    } else {
      throw nf7::Exception {"unknown command type: "+type};
    }
  } catch (nf7::Exception& e) {
    shared_->log.Error(e.msg());
  }

 private:
  nf7::Life<NFile>::Ref f_;

  std::shared_ptr<SharedData> shared_;

  std::optional<nf7::Future<std::shared_ptr<nf7::Mutex::Lock>>> lock_;


  void Push(const std::shared_ptr<nf7::Node::Lambda>& caller, bool ex, auto&& f) noexcept {
    auto self = shared_from_this();
    if (!lock_) {
      lock_ = f_->mtx_.AcquireLock(self, ex);
    }
    lock_->ThenIf([self, this, caller, f = std::move(f)](auto&) mutable {
      f_->th_->Push(self, NFile::Runner::Task {
        .callee = self,
        .caller = std::move(caller),
        .func   = std::move(f),
      });
    });
  }
};
std::shared_ptr<nf7::Node::Lambda> NFile::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<NFile::Lambda>(*this, parent);
}


void NFile::UpdateMenu() noexcept {
  if (ImGui::BeginMenu("config")) {
    static nf7::gui::ConfigEditor ed;
    ed(*this);
    ImGui::EndMenu();
  }
}
void NFile::UpdateTooltip() noexcept {
  ImGui::Text("npath: %s", mem_->npath.generic_string().c_str());
  ImGui::Text("mode : %s", mem_->mode.c_str());
}

}
}  // namespace nf7
