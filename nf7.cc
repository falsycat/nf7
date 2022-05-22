#include "nf7.hh"

#include <cassert>
#include <map>

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <imgui.h>


using namespace std::literals;


namespace nf7 {

static inline auto& registry_() noexcept {
  static std::map<std::string, File::TypeInfo*> registry_;
  return registry_;
}


void Exception::UpdatePanic() const noexcept {
  ImGui::TextUnformatted(msg_.c_str());
  ImGui::Text("from %s:%d", srcloc_.file_name(), srcloc_.line());
}

const std::map<std::string, const File::TypeInfo*>& File::registry() noexcept {
  return registry_();
}
const File::TypeInfo& File::registry(std::string_view name) {
  const auto  sname = std::string(name);
  const auto& reg   = registry_();

  auto itr = reg.find(sname);
  if (itr == reg.end()) throw NotFoundException("missing type: "+sname);
  return *itr->second;
}
File::File(const TypeInfo& t, Env& env) noexcept : type_(&t), env_(&env) {
}
File::~File() noexcept {
  assert(id_ == 0);
}
std::unique_ptr<File> File::Deserialize(Env& env, Deserializer& d) {
  std::string type;
  d(type);

  const auto& reg = registry_();
  auto itr = reg.find(type);
  if (itr == reg.end()) {
    throw DeserializeException("unknown actor type: "s+type);
  }
  return itr->second->Deserialize(env, d);
}
void File::Serialize(Serializer& s) const noexcept {
  s(std::string(type().name()));
  SerializeParam(s);
}
void File::MoveUnder(Id parent) noexcept {
  if (parent) {
    assert(parent_ == 0);
    assert(id_ == 0);
    parent_ = parent;
    id_     = env_->AddFile(*this);
  } else {
    assert(parent_ != 0);
    assert(id_ != 0);
    env_->RemoveFile(id_);
    id_     = 0;
    parent_ = 0;
  }
}
File& File::FindOrThrow(std::string_view name) const {
  if (auto ret = Find(name)) return *ret;
  throw NotFoundException("missing child: "+std::string(name));
}
File& File::ResolveOrThrow(const Path& p) const
try {
  assert(parent_ != 0);
  assert(id_ != 0);

  auto ret = const_cast<File*>(this);
  for (const auto& term : p.terms()) {
    if (term == "..") {
      ret = &env_->GetFile(parent_);
    } else if (term == ".") {
      // do nothing
    } else {
      ret = &ret->FindOrThrow(term);
    }
  }
  return *ret;
} catch (Exception&) {
  throw NotFoundException("failed to resolve path: "+p.Stringify());
}
File& File::ResolveOrThrow(std::string_view p) const {
  return ResolveOrThrow(Path::Parse(p));
}
File::Interface& File::ifaceOrThrow(const std::type_info& t) {
  if (auto ret = iface(t)) return *ret;
  throw NotImplementedException(t.name()+"is not implemented"s);
}

File::TypeInfo::TypeInfo(const std::string&                cat,
                         const std::string&                name,
                         std::unordered_set<std::string>&& flags) noexcept :
    cat_(cat), name_(name), flags_(std::move(flags)) {
  auto& reg = registry_();
  auto [itr, uniq] = reg.emplace(std::string(name_), this);
  assert(uniq);
}
File::TypeInfo::~TypeInfo() noexcept {
  auto& reg = registry_();
  reg.erase(std::string(name_));
}

bool File::Path::ValidateTerm(std::string_view term) noexcept {
  constexpr size_t kMaxTermSize = 256;
  if (term.size() > kMaxTermSize) {
    return false;
  }

  static const char kAllowedChars[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789_";
  if (term.find_first_not_of(kAllowedChars) != std::string_view::npos) {
    return false;
  }
  return true;
}
File::Path File::Path::Deserialize(Deserializer& d) {
  Path p;
  d(p.terms_);
  return p;
}
void File::Path::Serialize(Serializer& s) const noexcept {
  s(terms_);
}
File::Path File::Path::Parse(std::string_view p) {
  std::vector<std::string> ret;

  auto st = p.begin(), itr = st;
  for (; itr < p.end(); ++itr) {
    if (*itr == '/') {
      if (st < itr) ret.push_back(std::string {st, itr});
      st = itr + 1;
    }
  }
  if (st < itr) ret.push_back(std::string {st, itr});

  for (const auto& term : ret) {
    if (!ValidateTerm(term)) {
      throw DeserializeException("invalid term: "+term);
    }
  }
  return {std::move(ret)};
}
std::string File::Path::Stringify() const noexcept {
  std::string ret;
  for (const auto& term : terms_) {
    ret += "/"+term;
  }
  return ret;
}

Context::Context(Env& env, File::Id initiator, Context::Id parent) noexcept :
    env_(&env), initiator_(initiator), id_(env_->AddContext(*this)), parent_(parent) {
}
Context::~Context() noexcept {
  env_->RemoveContext(id_);
}

Env::Watcher::Watcher(Env& env) noexcept : env_(&env) {
}
Env::Watcher::~Watcher() noexcept {
  env_->RemoveWatcher(*this);
}
void Env::Watcher::Watch(File::Id id) noexcept {
  env_->AddWatcher(id, *this);
}

}  // namespace nf7
